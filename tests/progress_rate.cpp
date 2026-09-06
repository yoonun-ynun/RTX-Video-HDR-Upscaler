#include "progress_rate.h"
#include <iostream>
static void Equal(double actual,double expected) {
    if(std::abs(actual-expected)>1e-8) throw std::runtime_error("Unexpected rate");
}
int main() {
    try {
        ProgressRate constant;
        for(unsigned seconds=1;seconds<=120;seconds++) {
            auto r=constant.Observe(seconds,seconds*30);
            Equal(r.recent,30);Equal(r.average,30);
        }
        ProgressRate burst;
        for(unsigned s=1;s<=5;s++) burst.Observe(s,s*100);
        for(unsigned s=6;s<=10;s++) burst.Observe(s,500+(s-5)*20);
        auto r=burst.Observe(11,620);
        Equal(r.recent,20);Equal(r.average,620.0/11);
        r=burst.Observe(16,620);Equal(r.recent,0);Equal(r.average,620.0/16);
        ProgressRate slowStart;
        r=slowStart.Observe(3,1);Equal(r.recent,1.0/3);Equal(r.average,1.0/3);
        r=slowStart.Observe(20,2);Equal(r.recent,.2);Equal(r.average,.1);
        std::cout << "Constant speed, initial burst, stalled window and slow startup passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
