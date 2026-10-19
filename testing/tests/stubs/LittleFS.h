#pragma once
#include "Arduino.h"
struct FakeFS;
class File {
    FakeFS* fs=nullptr; std::shared_ptr<std::vector<uint8_t>> data; String filename; size_t pos=0,entry=0;
    bool directory=false; std::vector<String> names;
public:
    File()=default;
    File(FakeFS* fs,const String& path,std::shared_ptr<std::vector<uint8_t>> d):fs(fs),data(d),filename(path){}
    File(FakeFS* fs,std::vector<String> names):fs(fs),directory(true),names(names){}
    explicit operator bool()const {return bool(data)||directory;}
    bool isDirectory()const{return directory;}
    const char* path()const{return filename.c_str();}
    size_t size()const{return data?data->size():0;}
    size_t available()const{return data && pos<data->size()?data->size()-pos:0;}
    bool seek(size_t n){pos=n;return bool(data);}
    size_t read(uint8_t* b,size_t n){if(!data || pos>=data->size())return 0;n=std::min(n,data->size()-pos);memcpy(b,data->data()+pos,n);pos+=n;return n;}
    size_t write(const uint8_t* b,size_t n);
    void flush(){}
    void close(){data.reset();directory=false;}
    File openNextFile();
};
struct FakeFS {
    std::map<String,std::shared_ptr<std::vector<uint8_t>>> files;
    bool failWrites=false,failRename=false,mount=true;
    String failRemovePath;
    int removesUntilFailure=-1;
    size_t capacity=1024*1024;
    void reset(){files.clear();failWrites=failRename=false;mount=true;capacity=1024*1024;failRemovePath="";removesUntilFailure=-1;}
    bool begin(bool){return mount;}
    bool mkdir(const char*){return true;}
    bool exists(const String& p){return p=="/mesh3" || files.count(p);}
    size_t totalBytes(){return capacity;}
    size_t usedBytes(){size_t n=0;for(auto& x:files)n+=x.second->size();return n;}
    File open(const String& p,const char* mode="r"){
        if(p=="/mesh3"){std::vector<String> v;for(auto& f:files)if(f.first.find("/mesh3/")==0)v.push_back(f.first);return File(this,v);}
        if(mode[0]=='w'){if(failWrites)return {};files[p]=std::make_shared<std::vector<uint8_t>>();}
        auto it=files.find(p);return it==files.end()?File():File(this,p,it->second);
    }
    bool rename(const String& a,const String& b){if(failRename || !files.count(a))return false;files[b]=files[a];files.erase(a);return true;}
    bool remove(const String& path){if(path==failRemovePath || removesUntilFailure==0)return false;if(removesUntilFailure>0)--removesUntilFailure;return files.erase(path)!=0;}
};
inline FakeFS LittleFS;
inline size_t File::write(const uint8_t* b,size_t n){if(!data || fs->failWrites)return 0;if(pos+n>data->size())data->resize(pos+n);memcpy(data->data()+pos,b,n);pos+=n;return n;}
inline File File::openNextFile(){return entry<names.size()?fs->open(names[entry++],"r"):File();}
