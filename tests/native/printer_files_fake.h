uint32_t fileGeneration=1;bool filesOnline=true,fileWorker=true;
std::map<String,String> localGcodes;
class File {
    String bytes_;uint32_t generation_=0;size_t cursor_=0;bool exists_=false;
public:
    File()=default;explicit File(String bytes):bytes_(std::move(bytes)),generation_(fileGeneration),exists_(true){}
    explicit operator bool()const{return exists_&&filesOnline&&generation_==fileGeneration;}
    bool isDirectory()const{return false;}
    size_t size()const{return *this?bytes_.length():0;}
    size_t position()const{return cursor_;}
    bool seek(size_t at){if(!*this||at>bytes_.length())return false;cursor_=at;return true;}
    size_t read(uint8_t* target,size_t count){if(!*this)return 0;const auto n=std::min(count,bytes_.length()-cursor_);memcpy(target,bytes_.c_str()+cursor_,n);cursor_+=n;return n;}
    int read(){uint8_t c=0;return read(&c,1)?c:-1;}
    size_t available()const{return *this?bytes_.length()-cursor_:0;}
};
void esp_fill_random(void* p,size_t n){memset(p,123,n);}
namespace hp {
struct NetworkWorker {static NetworkWorker& instance(){static NetworkWorker s;return s;}bool isWorkerContext()const{return fileWorker;}};
struct FileBrowserService {static File openRead(const String& source){auto it=localGcodes.find(source);return it==localGcodes.end()?File():File(it->second);}};
struct DecodedImage {unsigned width=0,height=0;};
struct ImageCodec {
    static bool dimensions(const uint8_t* bytes,size_t size,uint32_t& w,uint32_t& h){return qoiDimensions(bytes,size,w,h);}
    static std::shared_ptr<DecodedImage> decode(uint8_t*,size_t,unsigned width,unsigned height,bool cover,const std::atomic<bool>*,bool center,bool packed){assert(width==320&&height==220&&!cover&&!center&&packed);return std::make_shared<DecodedImage>();}
};
struct ImageResourceService {
    static ImageResourceService& instance(){static ImageResourceService s;return s;}
    uint32_t store(const String&,std::shared_ptr<DecodedImage>){return 123;}
    bool release(const String&,uint32_t){return true;}
};
}
