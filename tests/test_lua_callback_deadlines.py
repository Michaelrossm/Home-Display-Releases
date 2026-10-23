from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]


class LuaCallbackDeadlineTests(unittest.TestCase):
    def test_real_protected_call_distinguishes_completion_from_vm_errors(self):
        source = (ROOT / 'src/apps/lua/LuaApp.cpp').read_text(encoding='utf-8')
        function = source.split('int protectedCall(', 1)[1].split('void openAppLibraries(', 1)[0]
        function = 'int protectedCall(' + function
        test_system_performance.SystemPerformanceTests().native(r'''
#include <string>
#include <cstdio>
#include <cstdint>
#include <cassert>
enum class LuaRuntimeState {Opening,Foreground,Suspended,BackgroundExecuting};
struct lua_State {};
struct LuaApp {
 LuaRuntimeState state=LuaRuntimeState::Foreground;
 LuaRuntimeState lifecycle(){return state;}
 const char* id(){return "printer_panel";}
 static LuaApp* self(lua_State*);
};
LuaApp app; bool owner=true; int rc=0,pops=0,reports=0;
LuaApp* LuaApp::self(lua_State*){return owner?&app:nullptr;}
uint32_t now=100,runMs=37;
struct String:std::string {
 using std::string::string;
 String(uint32_t n):std::string(std::to_string(n)){}
 String(std::string s):std::string(s){}
};
struct EventLog {static EventLog& instance(){static EventLog l;return l;}void addTransient(const char*,const char*,String){++reports;}};
using lua_Integer=long long;
constexpr int LUA_REGISTRYINDEX=0,LUA_MASKCOUNT=1,LUA_OK=0,LUA_ERRRUN=2;
constexpr int kDefaultCallbackMs=10,kDefaultHookCalls=250,kHookStride=2000;
int kCallStartKey,kBudgetRegistryKey,kWallBudgetKey;
lua_Integer pushed=0,wallBudget=0,instructionLimit=0;
uint32_t millis(){return now;} uint32_t micros(){return now*1000;}
void lua_pushinteger(lua_State*,lua_Integer n){pushed=n;}
void lua_rawsetp(lua_State*,int,void* key){
 if(key==&kWallBudgetKey)wallBudget=pushed;
 if(key==&kBudgetRegistryKey)instructionLimit=pushed;
}
void instructionBudget(lua_State*,void*){}
void lua_sethook(lua_State*,void(*)(lua_State*,void*),int,int){}
int lua_pcall(lua_State*,int,int,int){now+=runMs;return rc;}
void lua_pop(lua_State*,int n){pops+=n;}
void lua_pushliteral(lua_State*,const char*){}
''' + function + r'''
int main(){
 lua_State L;uint32_t elapsed=0;
 assert(protectedCall(&L,0,0,5,50,&elapsed)==LUA_OK);
 assert(elapsed==37000 && reports==1 && pops==0);
 assert(wallBudget==100 && instructionLimit==50);
 assert(protectedCall(&L,0,0,5,50)==LUA_OK && reports==1);
 app.state=LuaRuntimeState::Opening;rc=LUA_ERRRUN;
 assert(protectedCall(&L,0,0,5,50)==LUA_ERRRUN);
 rc=LUA_OK;app.state=LuaRuntimeState::BackgroundExecuting;
 assert(protectedCall(&L,0,2,10,50)==LUA_ERRRUN);
 assert(wallBudget==10 && instructionLimit==50);
 assert(pops==2);
 app.state=LuaRuntimeState::Suspended;
 assert(protectedCall(&L,0,0,10,50)==LUA_ERRRUN);
 owner=false;
 assert(protectedCall(&L,0,0,10,50)==LUA_ERRRUN);
 assert(wallBudget==10);
 owner=true;app.state=LuaRuntimeState::Foreground;runMs=101;
 assert(protectedCall(&L,0,0,12,60)==LUA_ERRRUN);
 runMs=1;assert(protectedCall(&L,0,0,5,50)==LUA_OK);
}
''')


if __name__ == '__main__':
    unittest.main()
