import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class FitzTouchTests(unittest.TestCase):

    def test_radio_requests_do_not_run_network_or_focus_on_ui_thread(self):
        service = (ROOT / "src/apps/radio/RadioService.cpp").read_text(encoding="utf-8")
        command = service.split("bool RadioService::command",1)[1].split("bool RadioService::setVolume",1)[0]
        self.assertIn("return queue", command)
        self.assertNotIn("preparePlayback", command)
        self.assertIn("reconfigurePending_=true;pending_=Request::None;catalogBusy_=true", service)
        self.assertIn("reconfigurePending_?Request::Reconfigure:pending_", service)
        self.assertIn("catalogBusy_=reconfigurePending_", service)
        worker = service.split("void RadioService::backgroundTick",1)[1]
        self.assertLess(worker.index("Request request"), worker.index("NetworkService::instance().online()"))
        self.assertIn('state_.error="Netzwerk offline"', worker)
        self.assertIn("PlayPause:client_.playControl(s.playStatus==2?2:1)", service)
        self.assertIn("Previous:client_.playControl(4)", service)
        settings = (ROOT / "src/apps/radio/RadioSettings.cpp").read_text(encoding="utf-8")
        self.assertIn('putSecretString("rad_pin"', settings)
        self.assertIn("settings.host.length() > 96", settings)


if __name__ == "__main__": unittest.main()
