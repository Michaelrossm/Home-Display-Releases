#pragma once

namespace hp {

class LvFsBridge {
public:
    static LvFsBridge& instance();
    bool begin();

private:
    bool registered_ = false;
};

}
