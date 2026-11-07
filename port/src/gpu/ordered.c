#include "ordered.h"
#include <math.h>
#include <string.h>
static int minInt(int a,int b){return a<b?a:b;}
static int maxInt(int a,int b){return a>b?a:b;}
OrderedBox orderedBounds(const void* vertices,size_t stride,int scale,int w,int h) {
    float lo[2]={INFINITY,INFINITY},hi[2]={-INFINITY,-INFINITY};
    for(int k=0;k<3;k++) {
        float p[2];memcpy(p,(const char*)vertices+k*stride,sizeof(p));
        for(int d=0;d<2;d++) {
            if(!isfinite(p[d]))return (OrderedBox){0,0,w,h};
            lo[d]=fminf(lo[d],p[d]);hi[d]=fmaxf(hi[d],p[d]);
        }
    }
    /* Pad for native/hi-res pixel-centre convention and rasterizer rounding.
     * Clamp as doubles before converting; huge clipped positions stay safe. */
    OrderedBox b;
    b.x0=(int)fmax(0,fmin(w,floor((double)lo[0]*scale)-1));
    b.y0=(int)fmax(0,fmin(h,floor((double)lo[1]*scale)-1));
    b.x1=(int)fmax(0,fmin(w,ceil((double)hi[0]*scale)+2));
    b.y1=(int)fmax(0,fmin(h,ceil((double)hi[1]*scale)+2));
    return b;
}
uint64_t orderedEstimate(const OrderedBox* boxes,int count,OrderedBox band) {
    uint64_t sum=0;
    for(int i=0;i<count;i++) {
        int w=minInt(boxes[i].x1,band.x1)-maxInt(boxes[i].x0,band.x0);
        int h=minInt(boxes[i].y1,band.y1)-maxInt(boxes[i].y0,band.y0);
        if(w>0&&h>0)sum+=(uint64_t)w*h;
    }
    return sum;
}
int orderedBandEnd(const OrderedBox* boxes,int count,OrderedBox col,int y,uint32_t capacity) {
    int width=col.x1-col.x0;
    if(width<=0||count<=0||count>ORDERED_TRIANGLES||y>=col.y1||width>(int)(capacity/(unsigned)count))return y;
    int lo=y+1,hi=minInt(col.y1,y+(int)(capacity/(unsigned)width));
    while(lo<hi) {
        int mid=lo+(hi-lo+1)/2;
        OrderedBox b={col.x0,y,col.x1,mid};
        if(orderedEstimate(boxes,count,b)<=capacity)lo=mid;else hi=mid-1;
    }
    return lo;
}

int orderedIndependent(const void* vertices,size_t stride,int count,size_t wOffset) {
    if(count==3)return 1;
    if(count!=6)return 0;
    float xy[6][2],w[6];
    for(int i=0;i<6;i++) {
        const char* p=(const char*)vertices+i*stride;
        memcpy(xy[i],p,sizeof(xy[i]));w[i]=1;
        if(wOffset)memcpy(&w[i],p+wOffset,sizeof(w[i]));
        if(!isfinite(xy[i][0])||!isfinite(xy[i][1])||!isfinite(w[i])||w[i]<=0)return 0;
    }
    /* Only a rectangle split along its diagonal is proven safe here.
     * Arbitrary thin adjacent triangles can flip after rasterizer snapping;
     * those still use ordered resolution. Equal W keeps shared axis values
     * identical through the hi-res vertex shader's multiply and divide. */
    float x0=xy[0][0],x1=x0,y0=xy[0][1],y1=y0;
    for(int i=1;i<6;i++) {
        if(w[i]!=w[0])return 0;
        x0=fminf(x0,xy[i][0]);x1=fmaxf(x1,xy[i][0]);
        y0=fminf(y0,xy[i][1]);y1=fmaxf(y1,xy[i][1]);
    }
    if(x0==x1||y0==y1)return 0;
    unsigned bits[2]={0,0};
    for(int i=0;i<6;i++) {
        if((xy[i][0]!=x0&&xy[i][0]!=x1)||(xy[i][1]!=y0&&xy[i][1]!=y1))return 0;
        unsigned corner=(xy[i][0]==x1?1u:0u)|(xy[i][1]==y1?2u:0u),bit=1u<<corner;
        if(bits[i/3]&bit)return 0;
        bits[i/3]|=bit;
    }
    unsigned common=bits[0]&bits[1];
    return (bits[0]|bits[1])==15u&&(common==9u||common==6u);
}
