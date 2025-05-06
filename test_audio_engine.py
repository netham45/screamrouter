#!/usr/bin/env python3

import time
import sys
import os
import logging
from typing import List

# Add project root to path to allow importing the compiled extension
project_root = os.path.dirname(os.path.abspath(__file__))
# Assuming the .so file is in the root directory alongside this script or setup.py placed it correctly
# If src is needed: sys.path.insert(0, os.path.join(project_root, 'src'))

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

try:
    # Import the C++ extension module and necessary classes
    from screamrouter_audio_engine import AudioManager, SinkConfig, SourceConfig, EQ_BANDS
    logging.info("Successfully imported screamrouter_audio_engine module.")
except ImportError as e:
    logging.error(f"Failed to import screamrouter_audio_engine: {e}")
    logging.error("Ensure the C++ extension is compiled and available in the Python path.")
    logging.error("Try running setup.py build_ext --inplace")
    sys.exit(1)

def run_test():
    """Runs a basic test of the AudioManager C++ extension."""
    logging.info("--- Starting Audio Engine Test ---")

    audio_manager = None
    try:
        # 1. Instantiate AudioManager
        logging.info("Instantiating AudioManager...")
        audio_manager = AudioManager()
        audio_manager.initialize(rtp_listen_port=40000)
        sink_config = SinkConfig()
        sink_config.id = "test_sink_1"
        sink_config.output_ip = "192.168.3.212"
        sink_config.output_port = 4010
        sink_config.samplerate = 48000
        sink_config.channels = 2
        sink_config.bitdepth = 16

        audio_manager.add_sink(sink_config)

        source_config = SourceConfig()
        source_config.tag = "172.17.0.2"
        source_config.initial_volume = .75
        source_config.initial_delay_ms = 0
        source_config.initial_eq = [1.0] * EQ_BANDS

        # Configure source and get the unique instance ID
        logging.info(f"Configuring source with tag '{source_config.tag}'...")
        source_instance_id = audio_manager.configure_source(source_config)
        if not source_instance_id:
            logging.error("Failed to configure source, received empty instance ID.")
            raise RuntimeError("Failed to configure source")
        logging.info(f"Source configured successfully with instance ID: {source_instance_id}")
         # Explicitly connect the source instance to the sink using IDs
        logging.info(f"Connecting source instance '{source_instance_id}' to sink '{sink_config.id}'...")
        if not audio_manager.connect_source_sink(source_instance_id, sink_config.id):
            logging.error(f"Failed to connect source instance '{source_instance_id}' to sink '{sink_config.id}'.")
            raise RuntimeError("Failed to connect source to sink")
        logging.info("Connection successful.")
        logging.info("Audio engine setup complete.")

        # --- Test Control Commands ---
        logging.info(f"--- Testing Control Commands for instance {source_instance_id} ---")
        time.sleep(0.5)

        logging.info("Updating volume to 0.5...")
        if not audio_manager.update_source_volume(source_instance_id, 0.5):
            logging.error("Failed to update volume.")
        time.sleep(0.5)

        logging.info("Updating volume back to 0.75...")
        if not audio_manager.update_source_volume(source_instance_id, 0.75):
            logging.error("Failed to update volume.")
        time.sleep(0.5)

        logging.info("Updating delay to 100ms...")
        if not audio_manager.update_source_delay(source_instance_id, 100):
            logging.error("Failed to update delay.")
        time.sleep(0.5)

        logging.info("Updating timeshift to -0.2 seconds...")
        if not audio_manager.update_source_timeshift(source_instance_id, -0.2):
             logging.error("Failed to update timeshift.")
        time.sleep(0.5)

        logging.info("Updating EQ (setting first band to 0.8)...")
        new_eq = [1.0] * EQ_BANDS
        new_eq[0] = 0.8
        if not audio_manager.update_source_equalizer(source_instance_id, new_eq):
             logging.error("Failed to update EQ.")
        time.sleep(0.5)

        logging.info("--- Control Command Tests Complete ---")

        # --- Test MP3 Data Retrieval ---
        # Note: We expect empty bytes unless MP3 encoding is fully working and audio is flowing
        mp3_output_filename = "test_output.mp3"
        logging.info(f"--- Testing MP3 Data Retrieval for sink {sink_config.id} (writing to {mp3_output_filename}) ---")
        # Clear the file if it exists from a previous run

        mp3_chunks_written = 0
        # Poll for a bit longer to potentially capture some data if audio starts flowing
        with open(mp3_output_filename, 'bw') as f:
            for _ in range(10000): # Poll 25 times (5 seconds total)
                mp3_chunk = audio_manager.get_mp3_data(sink_config.id)
                while not mp3_chunk: # Check if the chunk is not empty
                    if not mp3_chunk:
                        mp3_chunk = audio_manager.get_mp3_data(sink_config.id)
                        if not mp3_chunk:
                            time.sleep(0.001) # Wait between polls
                            continue
                    logging.info(f"Retrieved MP3 data chunk (size: {len(mp3_chunk)} bytes)")
                    try:
                        f.write(mp3_chunk)
                        logging.info(f"Wrote MP3 chunk to {mp3_output_filename}")
                        mp3_chunks_written += 1
                    except IOError as e:
                        logging.error(f"Error writing MP3 chunk to {mp3_output_filename}: {e}")
                time.sleep(0.001) # Wait between polls

        logging.info(f"--- MP3 Data Retrieval Test Complete ({mp3_chunks_written} chunks written to {mp3_output_filename}) ---")


        logging.info("Waiting before shutdown (Ctrl+C to exit early)...")
        time.sleep(10) # Reduced wait time

        # --- Cleanup ---
        # Disconnect source before removing
        logging.info(f"Disconnecting source instance '{source_instance_id}' from sink '{sink_config.id}'...")
        if not audio_manager.disconnect_source_sink(source_instance_id, sink_config.id):
             logging.error("Failed to disconnect source from sink.")
        time.sleep(0.5)

        # Remove Source Instance
        logging.info(f"Removing source instance '{source_instance_id}'...")
        if audio_manager.remove_source(source_instance_id):
            logging.info(f"Source instance '{source_instance_id}' removed successfully.")
        else:
            logging.error(f"Failed to remove source instance '{source_instance_id}'.")
        time.sleep(0.5)

        # Remove Sink
        logging.info(f"Removing Sink '{sink_config.id}'...")
        if audio_manager.remove_sink("test_sink_1"):
            logging.info("Sink 'test_sink_1' removed successfully.")
        else:
            logging.error("Failed to remove sink 'test_sink_1'.")
        logging.info("Test sequence complete.")
    except Exception as e:
        logging.exception(f"An error occurred during the test: {e}")
    finally:
        # 8. Shutdown AudioManager
        if audio_manager:
            logging.info("Shutting down AudioManager...")
            audio_manager.shutdown()
            logging.info("AudioManager shut down.")

    logging.info("--- Audio Engine Test Finished ---")

if __name__ == "__main__":
    run_test()
