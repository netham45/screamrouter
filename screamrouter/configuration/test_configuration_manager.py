import unittest
from unittest.mock import MagicMock, patch
from ipaddress import IPv4Address

from screamrouter.screamrouter_types.configuration import SinkDescription, RtpReceiverMapping
from screamrouter.configuration.configuration_manager import ConfigurationManager

class TestConfigurationManager(unittest.TestCase):
    def setUp(self):
        # Mock the dependencies for ConfigurationManager
        self.mock_websocket = MagicMock()
        self.mock_plugin_manager = MagicMock()
        self.mock_websocket_config = MagicMock()
        self.mock_audio_manager = MagicMock()

        # Mock the screamrouter_audio_engine module
        self.mock_screamrouter_audio_engine = MagicMock()
        self.mock_screamrouter_audio_engine.DesiredEngineState = MagicMock()
        self.mock_screamrouter_audio_engine.SinkConfig = MagicMock()
        self.mock_screamrouter_audio_engine.AppliedSinkParams = MagicMock()
        self.mock_screamrouter_audio_engine.AppliedSourcePathParams = MagicMock()
        self.mock_screamrouter_audio_engine.CppSpeakerLayout = MagicMock()
        self.mock_screamrouter_audio_engine.EQ_BANDS = 18

        # Patch the screamrouter_audio_engine import
        self.patcher = patch.dict('sys.modules', {'screamrouter_audio_engine': self.mock_screamrouter_audio_engine})
        self.patcher.start()

        # Create an instance of ConfigurationManager
        with patch('screamrouter.configuration.configuration_manager.ConfigurationManager._ConfigurationManager__load_config'):
            self.config_manager = ConfigurationManager(
                self.mock_websocket,
                self.mock_plugin_manager,
                self.mock_websocket_config,
                self.mock_audio_manager
            )

    def tearDown(self):
        self.patcher.stop()

    def test_rtp_receiver_mapping_resolution(self):
        """
        Tests that the RTP receiver mappings are correctly resolved into IP addresses and ports.
        """
        # Create mock sink descriptions
        receiver1 = SinkDescription(name="Receiver 1", ip=IPv4Address("192.168.1.101"), port=5001, protocol="rtp")
        receiver2 = SinkDescription(name="Receiver 2", ip=IPv4Address("192.168.1.102"), port=5002, protocol="rtp")
        multi_device_sink = SinkDescription(
            name="Multi-Device Sink",
            protocol="rtp",
            multi_device_mode=True,
            rtp_receiver_mappings=[
                RtpReceiverMapping(receiver_sink_name="Receiver 1", left_channel=0, right_channel=1),
                RtpReceiverMapping(receiver_sink_name="Receiver 2", left_channel=2, right_channel=3),
            ]
        )

        # Set the sinks in the configuration manager
        self.config_manager.sink_descriptions = [receiver1, receiver2, multi_device_sink]
        self.config_manager.source_descriptions = []
        self.config_manager.route_descriptions = []

        # Process the configuration
        self.config_manager._ConfigurationManager__process_configuration()

        # Translate the configuration to the C++ desired state
        cpp_state = self.config_manager._translate_config_to_cpp_desired_state()

        # Find the multi-device sink in the C++ state
        translated_sink = None
        for sink in cpp_state.sinks:
            if sink.sink_id == multi_device_sink.name:
                translated_sink = sink
                break
        
        self.assertIsNotNone(translated_sink)

        # Check that the rtp_receivers list was populated correctly
        self.assertTrue(hasattr(translated_sink.sink_engine_config, 'rtp_receivers'))
        rtp_receivers = translated_sink.sink_engine_config.rtp_receivers
        self.assertEqual(len(rtp_receivers), 2)

        # Verify the details of the resolved receivers
        self.assertEqual(rtp_receivers[0]['ip'], "192.168.1.101")
        self.assertEqual(rtp_receivers[0]['port'], 5001)
        self.assertEqual(rtp_receivers[0]['left_channel'], 0)
        self.assertEqual(rtp_receivers[0]['right_channel'], 1)

        self.assertEqual(rtp_receivers[1]['ip'], "192.168.1.102")
        self.assertEqual(rtp_receivers[1]['port'], 5002)
        self.assertEqual(rtp_receivers[1]['left_channel'], 2)
        self.assertEqual(rtp_receivers[1]['right_channel'], 3)

if __name__ == '__main__':
    unittest.main()