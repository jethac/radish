#include "kokoro_frontend.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
using namespace radish_frontend;
static void compare(const Boundary& a,const Boundary& b) {
 if(a.T!=b.T||a.frames!=b.frames||a.samples!=b.samples||a.ids!=b.ids||a.style!=b.style||a.speed!=b.speed)throw std::runtime_error("request metadata changed");
 const std::vector<float>* x[]={&a.asr,&a.f0,&a.n,&a.har,&a.durations};
 const std::vector<float>* y[]={&b.asr,&b.f0,&b.n,&b.har,&b.durations};
 const char* names[]={"asr","f0","n","har","durations"};
 bool ok=true;
 for(unsigned k=0;k<5;k++){
  if(x[k]->size()!=y[k]->size())throw std::runtime_error("shape changed");
  size_t different=0;double max_abs=0;
  for(size_t i=0;i<x[k]->size();i++)if(std::memcmp(&(*x[k])[i],&(*y[k])[i],4)){different++;max_abs=std::fmax(max_abs,std::fabs(double((*x[k])[i])-(*y[k])[i]));}
  std::printf("COMPARE %s words=%zu different=%zu max_abs=%.17g\n",names[k],x[k]->size(),different,max_abs);ok&=different==0;
 }
 if(!ok)throw std::runtime_error("strict fresh/persistent comparison differs");
}
int main(int argc,char** argv){try{
 if(argc!=8)throw std::runtime_error("library front source inputA inputB inputC threads");
 int threads=std::stoi(argv[7]); Session session(argv[1],argv[2],argv[3],threads);
 Boundary first;
 for(unsigned i=0;i<4;i++){
  const char* input=argv[i==3?4:4+i];
  Boundary fresh=generate_frontend(argv[1],argv[2],argv[3],input,512,threads);
  Boundary actual=session.generate(input,512);
  compare(fresh,actual);
  if(actual.timing_collected||actual.sessions_reused!=(i!=0))throw std::runtime_error("session lifecycle metadata");
  if(i==0)first=actual;
  if(i==3)compare(first,actual);
  std::printf("REQUEST_PASS index=%u T=%lld reused=%d\n",i,(long long)actual.T,actual.sessions_reused);
 }
 std::puts("PASS frontend request semantics; no timing collected");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
