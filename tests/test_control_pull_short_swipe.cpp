#include "../src/ui/ControlPull.h"
#include <cassert>
int main() {
    hp::ControlPull p;
    p.begin(600, 0, 0, 800, 0);
    assert(p.move(600, 145, 650));
    assert(p.finish(700)); // steady 145px pull must open
    p.begin(600, 760, 800, 800, 1000);
    assert(p.move(600, 600, 1650));
    assert(!p.finish(1700)); // steady upward swipe must close
    p.begin(600, 0, 0, 800, 0);
    assert(p.move(600, 120, 500));
    assert(!p.finish(800)); // no auto-open after paused gesture
    p.begin(600, 0, 0, 800, 0);
    assert(!p.move(620, 2, 100));
    assert(!p.finish(120)); // no accidental activation
}
