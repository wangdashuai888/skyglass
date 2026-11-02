#include "sweep_damage.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cmath>
int main() {
    double total=0, oldTotal=0;
    for(int radius : {218, 340}) for(int angle=0;angle<360;++angle) {
        const double pi=3.141592653589793;
        auto x=[&](double d){return int(std::lround(radius*std::sin(d*pi/180)));};
        auto y=[&](double d){return int(std::lround(-radius*std::cos(d*pi/180)));};
        const double next=std::fmod(angle+4.0*180/(pi*radius),360.0);
        const int ox=x(angle),oy=y(angle),nx=x(next),ny=y(next);
        std::vector<sweep_damage::Rect> regions;
        sweep_damage::regions(0,0,ox,oy,nx,ny,8,3,[&](auto r){regions.push_back(r);});
        assert(regions.size()==8);
        for(auto r:regions) { assert(r.x1<=r.x2 && r.y1<=r.y2); total+=(r.x2-r.x1+1)*(r.y2-r.y1+1); }
        // Every pixel within 2 px of BOTH beams must be repainted, including
        // round caps, quadrant crossings, and the 359 -> 0 degree wrap.
        for(int beam=0;beam<2;++beam) for(int t=0;t<=radius*4;++t) {
            double f=double(t)/(radius*4);
            int px=int(std::lround((beam?nx:ox)*f)), py=int(std::lround((beam?ny:oy)*f));
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                bool covered=false;
                for(auto r:regions) if(px+dx>=r.x1 && px+dx<=r.x2 && py+dy>=r.y1 && py+dy<=r.y2) covered=true;
                assert(covered);
            }
        }
        // Old 55-degree wedge bounding rectangle, including its 6 px padding.
        int minx=0,maxx=0,miny=0,maxy=0;
        for(double beam:{double(angle),next}) for(int i=0;i<=100;++i) {
            int px=x(beam-55.0*i/100),py=y(beam-55.0*i/100);
            minx=std::min(minx,px);maxx=std::max(maxx,px);miny=std::min(miny,py);maxy=std::max(maxy,py);
        }
        oldTotal+=(maxx-minx+13)*(maxy-miny+13);
    }
    printf("All beam coverage tests passed (720 angles across two screen sizes).\n");
    printf("Requested damage area: %.1f%% of previous wedge bounds (before LVGL merging).\n",100*total/oldTotal);
    assert(total < oldTotal * .3);
}
