using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;
using System.Management.Automation;
using System.Collections.ObjectModel;
using System.Diagnostics;
using Makaretu.Dns;
using Message = Makaretu.Dns.Message;

namespace ScreamRouterDesktop
{
    public class ZeroconfService : IDisposable
    {
        private const int MDNS_PORT = 5353;
        private const string MDNS_SERVICE_NAME = "_sink._scream._udp.local";
        private MulticastService? mdnsService;
        private DomainName serviceName;
        private UdpClient? queryClient;
        private CancellationTokenSource? cancellationTokenSource;
        private bool isRunning = false;

        // Windows Core Audio API imports for getting audio device information
        [DllImport("ole32.dll")]
        private static extern int CoCreateInstance(ref Guid clsid, IntPtr pUnkOuter, uint dwClsContext, ref Guid iid, out IntPtr ppv);

        [Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
        private class MMDeviceEnumeratorClass { }

        [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IMMDeviceEnumerator
        {
            int NotImpl1();
            int GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, out IMMDevice ppDevice);
        }

        [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IMMDevice
        {
            int Activate(ref Guid iid, uint dwClsCtx, IntPtr pActivationParams, out IntPtr ppInterface);
            int OpenPropertyStore(uint stgmAccess, out IPropertyStore ppProperties);
            int GetId(out string ppstrId);
        }

        [Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IPropertyStore
        {
            int GetCount(out int cProps);
            int GetAt(int iProp, out PropertyKey pkey);
            int GetValue(ref PropertyKey key, out PropVariant pv);
        }

        [Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IAudioClient
        {
            int Initialize(int ShareMode, uint StreamFlags, long hnsBufferDuration, long hnsPeriodicity, IntPtr pFormat, IntPtr AudioSessionGuid);
            int GetBufferSize(out uint pNumBufferFrames);
            int GetStreamLatency(out long phnsLatency);
            int GetCurrentPadding(out uint pNumPaddingFrames);
            int IsFormatSupported(int ShareMode, IntPtr pFormat, out IntPtr ppClosestMatch);
            int GetMixFormat(out IntPtr ppDeviceFormat);
            int GetDevicePeriod(out long phnsDefaultDevicePeriod, out long phnsMinimumDevicePeriod);
            int Start();
            int Stop();
            int Reset();
            int SetEventHandle(IntPtr eventHandle);
            int GetService(ref Guid riid, out IntPtr ppv);
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PropertyKey
        {
            public Guid fmtid;
            public int pid;
        }

        [StructLayout(LayoutKind.Explicit)]
        private struct PropVariant
        {
            [FieldOffset(0)] public ushort vt;
            [FieldOffset(8)] public IntPtr pointerValue;
            [FieldOffset(8)] public byte byteValue;
            [FieldOffset(8)] public short shortValue;
            [FieldOffset(8)] public int intValue;
            [FieldOffset(8)] public long longValue;
            [FieldOffset(8)] public float floatValue;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct WaveFormatEx
        {
            public ushort wFormatTag;
            public ushort nChannels;
            public uint nSamplesPerSec;
            public uint nAvgBytesPerSec;
            public ushort nBlockAlign;
            public ushort wBitsPerSample;
            public ushort cbSize;
        }

        private enum EDataFlow
        {
            eRender = 0,
            eCapture = 1,
            eAll = 2
        }

        private enum ERole
        {
            eConsole = 0,
            eMultimedia = 1,
            eCommunications = 2
        }

        // DNS record types and flags - matching Python implementation
        private const int DNS_TYPE_A = 1;  // A record type (IPv4 address)
        private const ushort DNS_FLAGS_QR_QUERY = 0x0000;  // Standard query
        private const ushort DNS_FLAGS_QR_RESPONSE = 0x8400;  // Response with AA flag set

        public ZeroconfService()
        {
            serviceName = new DomainName(MDNS_SERVICE_NAME);
        }

        public void Start()
        {
            if (isRunning)
                return;

            try
            {
                // Initialize mDNS service if needed
                if (mdnsService == null)
                {
                    mdnsService = new MulticastService();
                    // Set up event handler for mDNS queries
                    mdnsService.QueryReceived += OnQueryReceived;
                    Trace.WriteLine("mDNS service initialized");
                }
                
                // Initialize query client if needed
                if (queryClient == null)
                {
                    queryClient = new UdpClient();
                    queryClient.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
                    queryClient.Client.Bind(new IPEndPoint(IPAddress.Any, MDNS_PORT));
                    Trace.WriteLine("Query client initialized");
                }

                // Create new cancellation token source
                cancellationTokenSource = new CancellationTokenSource();
                
                // Start the mDNS service
                mdnsService.Start();
                
                // Start listening for audio settings queries
                Task.Run(() => ListenForAudioSettingsQueries(cancellationTokenSource.Token));
                
                isRunning = true;
                Trace.WriteLine("ZeroconfService started");
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"Error starting ZeroconfService: {ex.Message}");
                Stop();
            }
        }

        public void Stop()
        {
            if (!isRunning)
                return;

            // Cancel any running tasks
            cancellationTokenSource?.Cancel();
            cancellationTokenSource = null;

            // Stop the mDNS service and remove event handlers
            if (mdnsService != null)
            {
                // Remove the event handler so it won't respond to queries
                mdnsService.QueryReceived -= OnQueryReceived;
                mdnsService.Stop();
            }
            
            // Close the query client to stop responding to UDP queries
            if (queryClient != null)
            {
                queryClient.Close();
                queryClient = null;
            }
            
            isRunning = false;
            Trace.WriteLine("ZeroconfService stopped - all network listeners removed");
        }

        private void OnQueryReceived(object sender, MessageEventArgs e)
        {
            try
            {
                // Process each question in the query
                foreach (var question in e.Message.Questions)
                {
                    Trace.WriteLine($"DEBUG: Received query for {question.Name} (Type: {question.Type})");
                    
                    // Check if this is a query for our hostname
                    if (question.Name.ToString().Equals(MDNS_SERVICE_NAME, StringComparison.OrdinalIgnoreCase) ||
                        question.Name.ToString().Equals($"{MDNS_SERVICE_NAME}.", StringComparison.OrdinalIgnoreCase))
                    {
                        IPEndPoint remoteEndPoint = e.RemoteEndPoint;
                        Trace.WriteLine($"Received hostname query from {remoteEndPoint.Address} for {question.Name}");
                        
                        // Get the IP address for the interface that would be used to reach the remote endpoint
                        IPAddress localIp = GetLocalIPForRemote(remoteEndPoint.Address);
                        Trace.WriteLine($"Responding with local IP: {localIp}");
                        
                        // Create a response message with flags matching Python implementation (0x8400)
                        var response = new Message
                        {
                            Id = e.Message.Id,
                            QR = true,     // This is a response (bit 15)
                            Opcode = 0,    // Standard query (bits 11-14)
                            AA = true,     // Authoritative answer (bit 10)
                            TC = false,    // Not truncated (bit 9)
                            RD = false,    // Recursion not desired (bit 8)
                            RA = false,    // Recursion not available (bit 7)
                            Z = 0          // Reserved bits (4-6) should be zero
                        };
                        
                        // Copy the question
                        response.Questions.Add(question);
                        
                        // Add an A record with our IP address
                        var aRecord = new ARecord
                        {
                            Name = question.Name,
                            Address = localIp,
                            TTL = TimeSpan.FromHours(1) // 1 hour TTL
                        };
                        response.Answers.Add(aRecord);
                        
                        // Send via multicast service
                        mdnsService?.SendAnswer(response);
                        
                        // Also send directly to the requester using a raw UDP socket
                        try
                        {
                            using (var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp))
                            {
                                // Set socket options
                                socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, true);
                                socket.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.MulticastTimeToLive, 255);
                                
                                // Bind to the mDNS port
                                socket.Bind(new IPEndPoint(localIp, 0)); // Use any available port for sending
                                
                                // Convert to bytes
                                byte[] buffer = response.ToByteArray();
                                
                                // Send directly to the requester
                                socket.SendTo(buffer, new IPEndPoint(remoteEndPoint.Address, MDNS_PORT));
                                
                                Trace.WriteLine($"Direct UDP response sent to {remoteEndPoint.Address}:{MDNS_PORT}");
                            }
                        }
                        catch (Exception ex)
                        {
                            Trace.WriteLine($"Error sending direct UDP response: {ex.Message}");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"Error handling mDNS query: {ex.Message}");
            }
        }

        private IPAddress GetLocalIPForRemote(IPAddress remoteAddress)
        {
            try
            {
                // Use PowerShell to get the correct IP address
                using (PowerShell ps = PowerShell.Create())
                {
                    // Run the find-netroute command to get the interface that would be used to reach the remote address
                    ps.AddCommand("Find-NetRoute")
                        .AddParameter("RemoteIPAddress", remoteAddress.ToString());
                    
                    Collection<PSObject> results = ps.Invoke();
                    
                    if (results.Count > 0)
                    {
                        // Get the IPAddress property from the first result
                        foreach (PSObject result in results)
                        {
                            if (result.Properties["IPAddress"] != null)
                            {
                                string ipAddressString = result.Properties["IPAddress"].Value.ToString();
                                if (IPAddress.TryParse(ipAddressString, out IPAddress ipAddress))
                                {
                                    return ipAddress;
                                }
                            }
                        }
                    }
                }
                
                // Fall back to the socket method if PowerShell fails
                using (Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, 0))
                {
                    socket.Connect(remoteAddress, 65530);
                    IPEndPoint? endPoint = socket.LocalEndPoint as IPEndPoint;
                    return endPoint?.Address ?? IPAddress.Loopback;
                }
            }
            catch
            {
                // Fall back to the socket method if PowerShell fails
                using (Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, 0))
                {
                    socket.Connect(remoteAddress, 65530);
                    IPEndPoint? endPoint = socket.LocalEndPoint as IPEndPoint;
                    return endPoint?.Address ?? IPAddress.Loopback;
                }
            }
        }

        private async Task ListenForAudioSettingsQueries(CancellationToken cancellationToken)
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested && queryClient != null)
                {
                    var result = await queryClient.ReceiveAsync(cancellationToken);
                    var receivedBytes = result.Buffer;
                    var remoteEndPoint = result.RemoteEndPoint;

                    string receivedData = Encoding.ASCII.GetString(receivedBytes);
                    if (receivedData == "query_audio_settings")
                    {
                        // Get current audio settings
                        var audioSettings = GetCurrentAudioSettings();
                        if (audioSettings != null)
                        {
                            // Format response as key=value pairs separated by semicolons
                            string response = string.Join(";", 
                                $"bit_depth={audioSettings.BitDepth}",
                                $"sample_rate={audioSettings.SampleRate}",
                                $"channels={audioSettings.Channels}",
                                $"channel_layout={audioSettings.ChannelLayout}"
                            );

                            byte[] responseBytes = Encoding.ASCII.GetBytes(response);
                            await queryClient.SendAsync(responseBytes, responseBytes.Length, remoteEndPoint);
                        }
                    }
                }
            }
            catch (OperationCanceledException)
            {
                // Expected when cancellation is requested
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"Error in audio settings listener: {ex.Message}");
            }
        }

        public class AudioSettings
        {
            public int BitDepth { get; set; } = 16;
            public int SampleRate { get; set; } = 44100;
            public int Channels { get; set; } = 2;
            public string ChannelLayout { get; set; } = "stereo";
        }

        public AudioSettings? GetCurrentAudioSettings()
        {
            try
            {
                // Get default audio endpoint
                Guid IID_IMMDeviceEnumerator = typeof(IMMDeviceEnumerator).GUID;
                Guid CLSID_MMDeviceEnumerator = typeof(MMDeviceEnumeratorClass).GUID;

                int hr = CoCreateInstance(ref CLSID_MMDeviceEnumerator, IntPtr.Zero, 1, ref IID_IMMDeviceEnumerator, out IntPtr pEnumerator);
                if (hr != 0)
                {
                    return null;
                }

                IMMDeviceEnumerator enumerator = (IMMDeviceEnumerator)Marshal.GetObjectForIUnknown(pEnumerator);
                Marshal.Release(pEnumerator);

                hr = enumerator.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eMultimedia, out IMMDevice device);
                if (hr != 0)
                {
                    return null;
                }

                // Get audio client to query format
                Guid IID_IAudioClient = typeof(IAudioClient).GUID;
                hr = device.Activate(ref IID_IAudioClient, 1, IntPtr.Zero, out IntPtr pAudioClient);
                if (hr != 0)
                {
                    return null;
                }

                IAudioClient audioClient = (IAudioClient)Marshal.GetObjectForIUnknown(pAudioClient);
                Marshal.Release(pAudioClient);

                // Get mix format
                hr = audioClient.GetMixFormat(out IntPtr pFormat);
                if (hr != 0)
                {
                    return null;
                }

                WaveFormatEx format = Marshal.PtrToStructure<WaveFormatEx>(pFormat);
                Marshal.FreeCoTaskMem(pFormat);

                // Create audio settings from format
                var settings = new AudioSettings
                {
                    BitDepth = format.wBitsPerSample,
                    SampleRate = (int)format.nSamplesPerSec,
                    Channels = format.nChannels,
                    ChannelLayout = GetChannelLayoutName(format.nChannels)
                };

                return settings;
            }
            catch (Exception ex)
            {
                Trace.WriteLine($"Error getting audio settings: {ex.Message}");
                return null;
            }
        }

        private string GetChannelLayoutName(int channels)
        {
            return channels switch
            {
                1 => "mono",
                2 => "stereo",
                3 => "2.1",
                4 => "quad",
                5 => "4.1",
                6 => "5.1",
                7 => "6.1",
                8 => "7.1",
                _ => $"channels_{channels}"
            };
        }

        public void Dispose()
        {
            // First stop the service
            Stop();
            
            // Then clean up resources
            if (mdnsService != null)
            {
                mdnsService.QueryReceived -= OnQueryReceived;
                mdnsService.Dispose();
                mdnsService = null;
            }

            if (queryClient != null)
            {
                queryClient.Close();
                queryClient.Dispose();
                queryClient = null;
            }
            
            Trace.WriteLine("ZeroconfService disposed");
        }
    }
}
