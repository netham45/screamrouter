#ifndef SCREAMROUTER_AUDIO_PULSE_PULSE_PROTOCOL_H
#define SCREAMROUTER_AUDIO_PULSE_PULSE_PROTOCOL_H

#include <cstdint>

namespace screamrouter {
namespace audio {
namespace pulse {

// PulseAudio native protocol version supported by mainstream clients (PA 16+).
constexpr uint32_t kPulseProtocolVersion = 35;

// Version/feature flags (upper bits of version word in AUTH handshake).
constexpr uint32_t kProtocolFlagSHM = 0x80000000U;
constexpr uint32_t kProtocolFlagMemFd = 0x40000000U;
constexpr uint32_t kProtocolVersionMask = 0x0000FFFFU;

// Descriptor flags (subset).
constexpr uint32_t kDescriptorFlagSeekMask = 0x000000FFU;
constexpr uint32_t kDescriptorFlagStart = 0;
constexpr uint32_t kDescriptorFlagShmMask = 0xFF000000U;

// Pulse command identifiers used by the lean server implementation.
enum class Command : uint32_t {
    Error = 0,
    Timeout,
    Reply,

    CreatePlaybackStream,
    DeletePlaybackStream,
    CreateRecordStream,
    DeleteRecordStream,
    Exit,
    Auth,
    SetClientName,
    LookupSink,
    LookupSource,
    DrainPlaybackStream,
    Stat,
    GetPlaybackLatency,
    CreateUploadStream,
    DeleteUploadStream,
    FinishUploadStream,
    PlaySample,
    RemoveSample,

    GetServerInfo,
    GetSinkInfo,
    GetSinkInfoList,
    GetSourceInfo,
    GetSourceInfoList,
    GetModuleInfo,
    GetModuleInfoList,
    GetClientInfo,
    GetClientInfoList,
    GetSinkInputInfo,
    GetSinkInputInfoList,
    GetSourceOutputInfo,
    GetSourceOutputInfoList,
    GetSampleInfo,
    GetSampleInfoList,
    Subscribe,

    SetSinkVolume,
    SetSinkInputVolume,
    SetSourceVolume,

    SetSinkMute,
    SetSourceMute,

    CorkPlaybackStream,
    FlushPlaybackStream,
    TriggerPlaybackStream,

    SetDefaultSink,
    SetDefaultSource,

    SetPlaybackStreamName,
    SetRecordStreamName,

    KillClient,
    KillSinkInput,
    KillSourceOutput,

    LoadModule,
    UnloadModule,
    AddAutoloadObsolete,
    RemoveAutoloadObsolete,
    GetAutoloadInfoObsolete,
    GetAutoloadInfoListObsolete,

    GetRecordLatency,
    CorkRecordStream,
    FlushRecordStream,
    PrebufPlaybackStream,

    Request,
    Overflow,
    Underflow,
    PlaybackStreamKilled,
    RecordStreamKilled,
    SubscribeEvent,

    MoveSinkInput,
    MoveSourceOutput,
    SetSinkInputMute,

    SuspendSink,
    SuspendSource,

    SetPlaybackStreamBufferAttr,
    SetRecordStreamBufferAttr,

    UpdatePlaybackStreamSampleRate,
    UpdateRecordStreamSampleRate,

    PlaybackStreamSuspended,
    RecordStreamSuspended,
    PlaybackStreamMoved,
    RecordStreamMoved,

    UpdateRecordStreamProplist,
    UpdatePlaybackStreamProplist,
    UpdateClientProplist,
    RemoveRecordStreamProplist,
    RemovePlaybackStreamProplist,
    RemoveClientProplist,

    Started,

    Extension,

    GetCardInfo,
    GetCardInfoList,
    SetCardProfile,

    ClientEvent,
    PlaybackStreamEvent,
    RecordStreamEvent,

    PlaybackBufferAttrChanged,
    RecordBufferAttrChanged,

    SetSinkPort,
    SetSourcePort,

    SetSourceOutputVolume,
    SetSourceOutputMute,

    SetPortLatencyOffset,

    EnableSrbChannel,
    DisableSrbChannel,

    RegisterMemfdShmid,

    SendObjectMessage,

    CommandMax
};

constexpr uint32_t kChannelCommand = static_cast<uint32_t>(-1);

struct MessageDescriptor {
    uint32_t length = 0;
    uint32_t channel = kChannelCommand;
    uint32_t offset_hi = 0;
    uint32_t offset_lo = 0;
    uint32_t flags = 0;
};

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#endif // SCREAMROUTER_AUDIO_PULSE_PULSE_PROTOCOL_H
