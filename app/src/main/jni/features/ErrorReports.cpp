#include "ErrorReports.h"
#include "engine/Engine.h"
#include <GLES2/gl2.h>
#include <android/log.h>
#include <jni.h>
#include <unwind.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

namespace features { namespace errorreports {
namespace {
constexpr std::size_t textLimit=64*1024;
constexpr unsigned slotCount=16;
std::mutex reportMutex;
std::string directory;
unsigned nextSlot=0;
std::string lastKey;
thread_local const std::string* shaderAsset=nullptr;
void* (*originalGetProgram)(void*,int,const std::string&,const std::string&)=nullptr;
int (*originalCompile)(void*,int,const std::string&)=nullptr;
int (*originalParseError)(const std::string&,const void*)=nullptr;
std::string (*formattedErrors)(const void*)=nullptr;

std::string Limited(const std::string& text) {
    return text.size()<=textLimit?text:text.substr(0,textLimit)+"\n[truncated]\n";
}
struct Trace {std::string text;unsigned count=0;};
_Unwind_Reason_Code Frame(_Unwind_Context* context,void* data) {
    auto& trace=*static_cast<Trace*>(data);
    uintptr_t pc=_Unwind_GetIP(context)&~uintptr_t(1);
    if(!pc || trace.count>=24) return _URC_END_OF_STACK;
    Dl_info image{};
    char location[128];
    if(dladdr(reinterpret_cast<void*>(pc),&image)) {
        ::snprintf(location,sizeof(location),"#%u pc +0x%lx ",trace.count++,
            static_cast<unsigned long>(pc-reinterpret_cast<uintptr_t>(image.dli_fbase)));
        trace.text+=location;
        trace.text+=image.dli_fname?image.dli_fname:"?";
        if(image.dli_sname) {
            int status=0;
            char* demangled=abi::__cxa_demangle(image.dli_sname,nullptr,nullptr,&status);
            trace.text+=" : ";trace.text+=demangled?demangled:image.dli_sname;
            std::free(demangled);
        }
    } else {
        ::snprintf(location,sizeof(location),"#%u pc 0x%lx",trace.count++,static_cast<unsigned long>(pc));
        trace.text+=location;
    }
    trace.text+='\n';
    return _URC_NO_REASON;
}
bool WriteFile(const std::string& path,const std::string& contents) {
    const std::string temp=path+".tmp";
    FILE* file=std::fopen(temp.c_str(),"wb");
    if(!file) return false;
    bool ok=std::fwrite(contents.data(),1,contents.size(),file)==contents.size();
    if(std::fflush(file)!=0 || ::fsync(::fileno(file))!=0) ok=false;
    if(std::fclose(file)!=0) ok=false;
    if(ok) ok=std::rename(temp.c_str(),path.c_str())==0;
    if(!ok) std::remove(temp.c_str());
    return ok;
}
// Runs on the error's native thread, before control returns to the engine.
// No UI calls or signal recovery: preserve the engine's return values and lifetime.
void Report(const char* kind,const std::string& resource,const char* function,
            const char* engineLocation,const std::string& details) {
    std::lock_guard<std::mutex> lock(reportMutex);
    const std::string key=std::string(kind)+resource+Limited(details);
    if(directory.empty() || key==lastKey) return;
    if(::mkdir(directory.c_str(),0700)!=0 && errno!=EEXIST) {
        __android_log_print(ANDROID_LOG_ERROR,"SunchErrors","Cannot create report directory: %s",directory.c_str());
        return;
    }
    timeval now{};::gettimeofday(&now,nullptr);
    Trace trace;_Unwind_Backtrace(Frame,&trace);
    char time[96];
    ::snprintf(time,sizeof(time),"Timestamp (Unix): %lld.%06ld\n",static_cast<long long>(now.tv_sec),static_cast<long>(now.tv_usec));
    std::string report="MCPE 0.14.3 resource error report\nType: ";
    report+=kind;report+='\n';report+=time;
    report+="Resource: ";report+=resource.empty()?"[no filename supplied by engine]":resource;
    report+="\nFunction: ";report+=function;
    report+="\nEngine source: ";report+=engineLocation;
    report+="\nEngine SHA256: ef26c33fc8a1f554ab3e4ed0bf39bd656f376f73706f52f9a3e6bd21989ef402\n\n";
    report+=details;
    report+="\n\nNative stack (module-relative PCs; engine C++ source lines unavailable without debug symbols):\n";
    report+=trace.text;
    char name[32];::snprintf(name,sizeof(name),"error-%02u.txt",nextSlot);
    const std::string path=directory+"/"+name;
    if(!WriteFile(path,report)) {
        __android_log_print(ANDROID_LOG_ERROR,"SunchErrors","Cannot save report: %s",path.c_str());
        return;
    }
    nextSlot=(nextSlot+1)%slotCount;lastKey=key;
    // Survives a later engine crash; shown again after the next launch.
    const std::string receipt=std::string(name)+"\n"+time;
    if(!WriteFile(directory+"/pending.txt",receipt))
        __android_log_print(ANDROID_LOG_WARN,"SunchErrors","Cannot save pending report marker");
    __android_log_print(ANDROID_LOG_ERROR,"SunchErrors","%s in %s at %s; report: %s",kind,resource.c_str(),function,path.c_str());
}
struct AssetScope {
    const std::string* previous;
    explicit AssetScope(const std::string& asset):previous(shaderAsset) {shaderAsset=&asset;}
    ~AssetScope() {shaderAsset=previous;}
};
void* GetProgram(void* self,int type,const std::string& asset,const std::string& defines) {
    AssetScope scope(asset);
    return originalGetProgram(self,type,asset,defines);
}
std::string ShaderText(GLuint shader,bool source) {
    GLint length=0;glGetShaderiv(shader,source?GL_SHADER_SOURCE_LENGTH:GL_INFO_LOG_LENGTH,&length);
    if(length<=1) return source?"[driver returned no source]":"[driver returned no compiler log]";
    std::vector<char> buffer(std::min<std::size_t>(static_cast<std::size_t>(length),textLimit)+1,0);
    GLsizei written=0;
    if(source) glGetShaderSource(shader,static_cast<GLsizei>(buffer.size()),&written,buffer.data());
    else glGetShaderInfoLog(shader,static_cast<GLsizei>(buffer.size()),&written,buffer.data());
    return std::string(buffer.data(),std::min<std::size_t>(std::max(written,0),buffer.size()-1));
}
std::string Numbered(const std::string& text) {
    std::string result="1 | ";unsigned line=1;
    for(std::size_t i=0;i<text.size();++i) {
        result+=text[i];
        if(text[i]=='\n' && i+1<text.size()) {result+=engine::Number(++line);result+=" | ";}
    }
    return Limited(result);
}
int Compile(void* self,int type,const std::string& source) {
    // ShaderProgramBase strings: variant at +0, resolved file path at +4.
    // getShaderProgram supplies the resource path before the platform resolves it.
    std::string resource=engine::At<std::string>(self,4);
    if(shaderAsset && shaderAsset->find('\n')==std::string::npos) resource=Limited(*shaderAsset);
    if(resource.empty()) resource="[inline shader]";
    int result;
    try {result=originalCompile(self,type,source);}
    catch(const std::exception& error) {
        Report("GLSL preprocessing",resource,"mce::ShaderProgramOGL::ShaderProgramOGL -> internal compile [RVA 0x2acf98]",
            "src-deps/Renderer/HAL/OGL/ShaderProgramOGL.cpp",std::string("Exception: ")+error.what()+"\nOriginal source:\n"+Numbered(Limited(source)));
        throw;
    }
    const GLuint shader=engine::At<GLuint>(self,12);
    GLint success=GL_TRUE;
    if(shader) glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if(!shader || success==GL_FALSE) {
        std::string details="Stage: ";details+=type==0?"vertex":type==1?"fragment":"unknown";
        details+="\nResolved path: ";details+=engine::At<std::string>(self,4);
        details+="\nVariant / defines: ";details+=Limited(engine::At<std::string>(self,0));
        details+="\nCompiler log:\n";details+=shader?ShaderText(shader,false):"glCreateShader returned 0";
        details+="\n\nSubmitted GLSL (numbered; includes/defines/version are expanded; driver #line directives take precedence):\n";
        details+=shader?Numbered(ShaderText(shader,true)):"[no shader object]";
        details+="\n\nOriginal resource source (numbered, before include expansion):\n";details+=Numbered(Limited(source));
        Report("GLSL compilation",resource,"mce::ShaderProgramOGL::ShaderProgramOGL -> internal compile [RVA 0x2acf98]",
            "src-deps/Renderer/HAL/OGL/ShaderProgramOGL.cpp:108 (built-in error site)",details);
    }
    return result;
}
int ParseError(const std::string& filename,const void* reader) {
    // Both call sites pass (filename, Json::Reader*), verified in ARM code.
    const std::string details=reader?formattedErrors(reader):"[no reader supplied]";
    Report("JSON UI parsing",filename,"UIDefRepository::reportParseError [RVA 0x2d8e58]",
        "src/common/client/gui/UIDefRepository.cpp:94-97 (built-in error site)",
        "Parser diagnostics (resource line and column):\n"+Limited(details));
    return originalParseError(filename,reader);
}
const engine::Hook hooks[]={
    {"_ZN3mce11ShaderGroup16getShaderProgramENS_10ShaderTypeERKSsS3_",0x48924c,"2de9f04fbbb00df1",reinterpret_cast<void*>(GetProgram),reinterpret_cast<void**>(&originalGetProgram)},
    {"ShaderProgramOGL internal compile",0x2acf98,"2de9f04f1446a24a",reinterpret_cast<void*>(Compile),reinterpret_cast<void**>(&originalCompile),true},
    {"UIDefRepository internal reportParseError",0x2d8e58,"2de9f04fadf5457d",reinterpret_cast<void*>(ParseError),reinterpret_cast<void**>(&originalParseError),true},
};
}
void InstallVerified(void* library) {
    formattedErrors=reinterpret_cast<decltype(formattedErrors)>(dlsym(library,"_ZNK4Json6Reader25getFormattedErrorMessagesEv"));
    if(!formattedErrors || !engine::InstallHooks(library,hooks,sizeof(hooks)/sizeof(hooks[0]))) return;
    __android_log_print(ANDROID_LOG_INFO,"SunchErrors","Verified GLSL and JSON UI diagnostics installed");
}
void RecordUiError(const std::string& file,const std::string& function,const std::string& details) {
    Report("JSON UI parameters",file,function.c_str(),
        "UIResolvedDef / UIControlFactory (verified ARMv7 engine; see function RVA)",details);
}
void Configure(const char* path) {
    std::lock_guard<std::mutex> lock(reportMutex);
    directory=path?path:"";
    // Reuse a missing/oldest slot, so restarts retain the most recent reports.
    time_t oldest=0;
    for(unsigned i=0;i<slotCount;++i) {
        char name[32];::snprintf(name,sizeof(name),"/error-%02u.txt",i);
        struct stat info{};
        if(::stat((directory+name).c_str(),&info)!=0) {nextSlot=i;break;}
        if(i==0 || info.st_mtime<oldest) {oldest=info.st_mtime;nextSlot=i;}
    }
}
std::string Pending() {
    std::lock_guard<std::mutex> lock(reportMutex);
    if(directory.empty()) return {};
    FILE* file=std::fopen((directory+"/pending.txt").c_str(),"rb");
    if(!file) return {};
    char marker[160]{};const std::size_t size=std::fread(marker,1,sizeof(marker)-1,file);std::fclose(file);
    const std::string receipt(marker,size);
    const std::size_t separator=receipt.find('\n');
    if(separator==std::string::npos) return {};
    const std::string name=receipt.substr(0,separator);
    // Only hand Java one of our fixed filenames; do not consume on read.
    for(unsigned i=0;i<slotCount;++i) {
        char expected[32];::snprintf(expected,sizeof(expected),"error-%02u.txt",i);
        if(name==expected) return directory+"/"+receipt;
    }
    return {};
}
void Acknowledge(const char* path) {
    std::lock_guard<std::mutex> lock(reportMutex);
    FILE* file=std::fopen((directory+"/pending.txt").c_str(),"rb");
    if(!file) return;
    char marker[160]{};const std::size_t size=std::fread(marker,1,sizeof(marker)-1,file);std::fclose(file);
    // A newer error may arrive while the previous dialog is still open.
    if(path && directory+"/"+std::string(marker,size)==path) std::remove((directory+"/pending.txt").c_str());
}
}}

extern "C" JNIEXPORT void JNICALL Java_com_mcpe_sunch_client0143_ErrorReports_nativeConfigure(JNIEnv* env,jclass,jstring path) {
    if(!path) return;
    const char* value=env->GetStringUTFChars(path,nullptr);
    if(value) {features::errorreports::Configure(value);env->ReleaseStringUTFChars(path,value);}
}
extern "C" JNIEXPORT jstring JNICALL Java_com_mcpe_sunch_client0143_ErrorReports_nativePending(JNIEnv* env,jclass) {
    const std::string path=features::errorreports::Pending();
    return path.empty()?nullptr:env->NewStringUTF(path.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_com_mcpe_sunch_client0143_ErrorReports_nativeAcknowledge(JNIEnv* env,jclass,jstring path) {
    if(!path) return;
    const char* value=env->GetStringUTFChars(path,nullptr);
    if(value) {features::errorreports::Acknowledge(value);env->ReleaseStringUTFChars(path,value);}
}
