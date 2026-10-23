from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class RadioTransportTests(unittest.TestCase):
    def test_transport_mapping_and_no_automatic_replay(self):
        service=(ROOT/'src/apps/radio/RadioService.cpp').read_text(encoding='utf-8')
        client=(ROOT/'src/apps/radio/engine/RadioClient.cpp').read_text(encoding='utf-8')
        self.assertIn('Next:client_.playControl(3)',service)
        self.assertIn('Previous:client_.playControl(4)',service)
        self.assertIn('!transient || !replaySafe',client)
        setter=client.split('bool RadioClient::apiSet',1)[1].split('bool RadioClient::listGetNext',1)[0]
        self.assertIn('1800,false',setter)
        self.assertIn('RADIO_COMMAND_FAILED',service)
        self.assertIn('commandError_',service.split('RadioState RadioService::snapshot',1)[1].split('bool RadioService::command',1)[0])
        self.assertIn('response.substring(start+8,end)',client)
