#include "SpectralMorph.h"
#include <chrono>
#include <iostream>
#include <vector>

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace vocalstack;
    std::cout << "sample_rate,mode,audio_seconds,elapsed_seconds,one_core_percent,max_block_us,checksum\n";
    for (double rate : {44100.,48000.,96000.}) for (int mode : {0,1}) {
        const size_t count = size_t(rate * 2);
        std::vector<std::array<float,4>> input(count);
        for (size_t i=0;i<count;++i) input[i] = {float(.1*std::sin(i*.07)),float(.1*std::cos(i*.09)),float(.08*std::sin(i*.11)),float(.06*std::cos(i*.13))};
        SpectralMorph effect; effect.prepare(rate);
        SpectralMorphSettings settings; settings.mode=mode; settings.morph=.75f;
        constexpr std::array<size_t,6> blocks{1,17,137,256,3,511};
        double checksum=0, maximum=0;
        const auto start=Clock::now();
        for(size_t offset=0,block=0;offset<count;++block) {
            const size_t n=std::min(blocks[block%blocks.size()],count-offset);
            const auto before=Clock::now();
            for(size_t j=0;j<n;++j) {
                const auto& x=input[offset+j];
                const auto y=effect.process({x[0],x[1]},{x[2],x[3]},true,settings);
                checksum+=y[0]+y[1];
            }
            maximum=std::max(maximum,std::chrono::duration<double,std::micro>(Clock::now()-before).count());
            offset+=n;
        }
        const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
        std::cout << rate << ',' << mode << ",2," << elapsed << ',' << elapsed/2*100 << ',' << maximum << ',' << checksum << '\n';
    }
}
