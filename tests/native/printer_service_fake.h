constexpr int pdTRUE=1,portMAX_DELAY=-1;using SemaphoreHandle_t=bool*;bool busyLock=false;
SemaphoreHandle_t xSemaphoreCreateMutex(){return new bool(false);}
int xSemaphoreTake(SemaphoreHandle_t lock,int wait){if(wait==0&&busyLock)return 0;assert(!*lock);*lock=true;return 1;}
void xSemaphoreGive(SemaphoreHandle_t lock){assert(*lock);*lock=false;}
namespace hp {
enum class ForegroundLoad{Normal,High};bool workerContext=true,networkOnline=true,tabletIsLocked=false;unsigned queuedPriority=0;ForegroundLoad load=ForegroundLoad::Normal;
struct NetworkWorker{using Fn=void(*)(void*);static NetworkWorker& instance(){static NetworkWorker w;return w;}
 bool addMaintenance(Fn,void*){return true;}bool isWorkerContext()const{return workerContext;}bool backgroundWindow()const{return workerContext;}bool queueEmpty()const{return true;}unsigned priorityDepth()const{return queuedPriority;}};
struct NetworkService{static NetworkService& instance(){static NetworkService s;return s;}bool online()const{return networkOnline;}};
struct SecurityService{static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked()const{return tabletIsLocked;}};
struct AppBackgroundScheduler{static AppBackgroundScheduler& instance(){static AppBackgroundScheduler s;return s;}ForegroundLoad foregroundLoad()const{return load;}bool isForeground(const char*)const{return false;}};
std::map<String,String> kv;unsigned events=0,networkRequests=0;std::vector<String> urls;
String luaStringForNative(const String& owner,const String& name){assert(workerContext);return kv[owner+":"+name];}
std::map<String,String> secrets;
String luaSecretForNative(const String& owner,const String& key){assert(workerContext);return secrets[owner+":"+key];}
void publishLuaEvent(const char*,const char*,uint32_t){++events;}
}
