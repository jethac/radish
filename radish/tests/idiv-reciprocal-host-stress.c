#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
static uint64_t denom_count, seed_count, quotient_count, overs, old_failures;
static double max_error_ratio, max_gap;
static uint32_t high(uint32_t x,uint32_t y){return ((uint64_t)x*y)>>32;}
static uint32_t refine(uint32_t r,uint32_t d){uint64_t prod=(uint64_t)r*d;uint32_t lo=prod;uint32_t err=(prod>>32)?lo:0u-lo;uint32_t c=high(r,err);return (prod>>32)?r-c-1:r+c;}
static uint32_t step(uint32_t r,uint32_t n,uint32_t d,int modulo){uint32_t q=high(n,r),rem=n-q*d;for(int k=0;k<2;k++){if(rem>=d){q++;rem-=d;}}return modulo?rem:q;}
static void check(uint32_t d){if(!d)return;denom_count++;float den=(float)d;float exact=1.0f/den;uint32_t b;memcpy(&b,&exact,4);for(int delta=-4;delta<=4;delta++){uint32_t u=b+delta;float rec;memcpy(&rec,&u,4);volatile float scaled=rec*4294966784.0f;float clamped=fminf(scaled,4294967040.0f);uint32_t r=(uint32_t)clamped;seed_count++;uint64_t p=(uint64_t)r*d;if((p>>32)>1){fprintf(stderr,"product high >1\n");exit(1);}overs+=(p>>32)!=0;uint32_t rr=refine(r,d);double s=4294967296.0/(double)d,e=(double)r-s,gap=s-(double)rr,ratio=e*e/s;if(ratio>max_error_ratio)max_error_ratio=ratio;if(gap>max_gap)max_gap=gap;
if((uint64_t)rr*d>UINT64_C(4294967296)||gap<0||gap>=2){fprintf(stderr,"invariant d=%u delta=%d r=%u rr=%u gap=%.17g\n",d,delta,r,rr,gap);exit(1);}
uint32_t ns[12]={0,1,d-1,d,d==UINT32_MAX?d:d+1,0x7fffffff,0x80000000,0xfffffffe,0xffffffff,d<=0x7fffffff?2*d-1:d,((UINT32_MAX/d)*d),((UINT32_MAX/d)*d)-1};
for(int j=0;j<12;j++){uint32_t n=ns[j];uint32_t q=step(rr,n,d,0),rem=step(rr,n,d,1);quotient_count++;if(q!=n/d||rem!=n%d){fprintf(stderr,"wrong d=%u n=%u delta=%d q=%u expect=%u r=%u expectr=%u\n",d,n,delta,q,n/d,rem,n%d);exit(1);}uint32_t old=r+high(r,0u-r*d);old_failures+=(step(old,n,d,0)!=n/d||step(old,n,d,1)!=n%d);}
}}
int main(void){for(uint32_t d=1;d<=65535;d++)check(d);for(unsigned k=0;k<32;k++){uint64_t c=UINT64_C(1)<<k;for(int o=-128;o<=128;o++){uint64_t x=c+o;if(x&&x<=UINT32_MAX)check((uint32_t)x);}}for(uint64_t q=1;q<=65535;q++){uint64_t d=UINT64_C(4294967296)/q;for(int o=-2;o<=2;o++){uint64_t x=d+o;if(x&&x<=UINT32_MAX)check((uint32_t)x);}}uint32_t rng=0x91ad563b;for(int i=0;i<1000000;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;check(rng);}printf("{\"denominator_samples\":%"PRIu64",\"seed_samples\":%"PRIu64",\"quotient_remainder_checks\":%"PRIu64",\"overshoot_seeds\":%"PRIu64",\"old_lowering_failures\":%"PRIu64",\"max_initial_e_squared_over_s\":%.17g,\"max_refined_gap\":%.17g,\"reciprocal_nearest_ulp_perturbations\":[-4,-3,-2,-1,0,1,2,3,4],\"status\":\"PASS finite host stress; not exhaustive32bit or actualnativeRCP measurement\"}\n",denom_count,seed_count,quotient_count,overs,old_failures,max_error_ratio,max_gap);}
