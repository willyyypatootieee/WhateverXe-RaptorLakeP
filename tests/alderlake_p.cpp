// Host fixtures for the read-only ADL-P GT profile and eDP evidence.
// AlderLakeP mirrors RaptorLakeP: same XeLP core, same Display 13 (XE_LPD)
// engine, catalogued without a GT backend. Recognition and readout only.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "kern_alderlake_p.hpp"

struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<uint32_t> reads;
    uint32_t limit=0x1000000;
    unsigned powerReads=0, changePowerAt=0;
    bool readable(uint32_t offset) const { return offset <= limit-4; }
    uint32_t read(uint32_t offset) {
        assert(offset < 0x163000 || offset >= 0x164000);
        reads.push_back(offset);
        if(offset==0x45404 && ++powerReads==changePowerAt)return regs[offset]^0x400;
        return regs[offset];
    }
    bool seen(uint32_t offset) const {
        for(auto r:reads)if(r==offset)return true;
        return false;
    }
};
struct GTIO : IO {
    std::vector<AlderLakeN::Register> writes;
    bool failGT=false,failReleaseGT=false;
    uint32_t read(uint32_t offset) {
        if(offset!=0xA188 && offset!=0xA278 && offset!=0x130044 && offset!=0xD84)
            assert((regs[0x130044]&1) && (regs[0xD84]&1));
        return IO::read(offset);
    }
    void write(uint32_t offset,uint32_t value) {
        assert(offset==0xA188 || offset==0xA278);writes.push_back({offset,value});
        const uint32_t mask=value>>16;
        regs[offset]=((regs[offset]&0xFFFF)&~mask)|(value&mask)|(mask<<16);
        const auto ack=offset==0xA188 ? 0x130044 : 0xD84;
        if((value&1) && !(offset==0xA188 && failGT))regs[ack]|=1;
        if(!(value&1) && !(offset==0xA188 && failReleaseGT))regs[ack]&=~1U;
    }
    void delay() {}
};
// 96-EU Xe-LP: six DSS, all EU pairs, VDBOX and VEBOX enabled (i7-12700H class).
static GTIO firmwareGT() {
    GTIO io;io.regs={{0x9138,1},{0x913c,0x3f},{0x9134,0},{0x9118,0},{0x9140,0xe00fa}};
    return io;
}
static IO eDP() {
    IO io;io.regs={{0x45404,0x401},{0x60400,0x8a000000},{0xc7200,0x80000008},
        {0xc8250,0x80000000},{0x64000,0x80000001}};
    return io;
}
int main() {
    using AlderLakeP::EDPStatus;
    using AlderLakeN::GTStatus;
    unsigned catalogued=0;
    for(const auto &entry:XELP::DeviceTable) {
        const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
        const bool adlp=entry.family==XELP::Family::AlderLakeP;
        assert(AlderLakeP::family(pci)==adlp);
        if(!adlp)continue;
        ++catalogued;
        // Recognition only: no GT backend, donor, admission or probe ABI.
        for(uint32_t revision:{0U,4U,8U,12U}) {
            const auto p=XELP::identify(pci,revision);
            assert(p.display==XELP::Display::XeLPD && p.gt==XELP::GT::Unknown);
            assert(p.donor==XELP::Donor::Unknown && !p.admitted() && !AlderLakeN::matches(pci,revision));
            XELP::ProbeConfig c;c.profile=p;c.combo=c.comboBackend=1;assert(!c.valid());
        }
        assert(!AlderLakeP::family((pci&0xFFFF0000U)|0x10DE));
    }
    // Every 46A*/46B*/46C* ADL-P entry, including the 46A6 in this laptop.
    assert(catalogued==6 && AlderLakeP::family(0x46A68086));
    assert(!AlderLakeP::family(0x46D18086) && !AlderLakeP::family(0x9A498086) && !AlderLakeP::family(0xA7A88086));
    auto step=AlderLakeP::stepping(4);
    assert(!strcmp(step.gt,"C0") && !strcmp(step.display,"E0"));
    for(uint32_t revision:{0U,1U,3U,5U,8U,12U,255U}) {
        step=AlderLakeP::stepping(revision);assert(!step.gt && !step.display);
    }
    std::puts("AlderLakeP catalogue recognition, no admission and Linux revision-4 steppings passed");

    AlderLakeP::Resources res;
    assert(AlderLakeP::resources(1,0x3f,0,0,0xe00fa,res));
    assert(res.slices==1 && res.dss==6 && res.nativeSubslices==12 && res.eus==96);
    assert(res.l3BankPairs==0xf && res.vdbox==0x5 && res.vebox==0x1);
    assert(!AlderLakeP::resources(1,0x7f,0,0,0xe00fa,res));
    assert(!AlderLakeP::resources(1,0,0,0,0xe00fa,res) && !AlderLakeP::resources(2,0xf,0,0,0xe00fa,res));
    assert(!AlderLakeP::resources(1,0xf,0xff,0,0xe00fa,res) && !AlderLakeP::resources(1,0xf,0,0xf,0xe00fa,res));
    std::puts("AlderLakeP SSEU, L3 bank-pair and media fuse decoding fixtures passed");

    auto gt=firmwareGT();gt.regs[0xA188]=8;gt.regs[0x130044]=8;
    auto ready=AlderLakeP::readGT(gt,true);
    assert(ready.forcewake.status==GTStatus::Complete && ready.forcewake.writes==4);
    assert(ready.forcewake.requestedMask==3 && ready.forcewake.releasedMask==3 && !ready.forcewake.releaseFailedMask);
    assert(ready.resources.eus==96 && ready.resources.dss==6 && ready.resources.nativeSubslices==12);
    assert(ready.resources.l3BankPairs==0xf && ready.resources.vdbox==5 && ready.resources.vebox==1);
    // AlderLakeN's small-GT capacity is never published for this profile.
    assert(!ready.forcewake.resources.eus && !ready.forcewake.resources.l3Banks && !ready.forcewake.resources.urbKiB);
    assert((gt.regs[0xA188]&0xFFFF)==8 && !(gt.regs[0x130044]&1) && !(gt.regs[0xD84]&1));
    for(auto w:gt.writes)assert(w.value==0x00010001 || w.value==0x00010000);
    auto excludedGT=firmwareGT();
    assert(AlderLakeP::readGT(excludedGT,false).forcewake.status==GTStatus::NotAdmitted && excludedGT.reads.empty());
    auto busyGT=firmwareGT();busyGT.regs[0xD84]=1;
    auto busy=AlderLakeP::readGT(busyGT,true);
    assert(busy.forcewake.status==GTStatus::ExistingOwner && busyGT.writes.empty() && !busy.resources.eus);
    auto unavailableGT=firmwareGT();unavailableGT.regs[0x9140]=UINT32_MAX;
    auto unavailable=AlderLakeP::readGT(unavailableGT,true);
    assert(unavailable.forcewake.status==GTStatus::Unavailable && !unavailable.resources.eus);
    assert(unavailable.forcewake.releasedMask==3);
    auto stuckGT=firmwareGT();stuckGT.failReleaseGT=true;
    auto stuck=AlderLakeP::readGT(stuckGT,true);
    assert(stuck.forcewake.status==GTStatus::ReleaseFailed && stuck.forcewake.releaseFailedMask==1 && !stuck.resources.eus);
    std::puts("AlderLakeP GT forcewake ownership, release and profile re-evaluation fixtures passed");

    IO absent;
    const AlderLakeN::DisplayReadout none;
    assert(AlderLakeP::readEDP(absent,none).status==EDPStatus::NotAdmitted && absent.reads.empty());
    auto panel=eDP();
    const auto boot=AlderLakeN::readDisplay(panel,true);
    assert(boot.status==AlderLakeN::ReadoutStatus::Complete && boot.poweredPipes==1 && boot.activePipes==1 && !boot.hdmiBPipes);
    panel.reads.clear();
    auto edp=AlderLakeP::readEDP(panel,boot);
    assert(edp.status==EDPStatus::Complete && edp.dpPipes==1 && edp.ddiAPipes==1 && edp.count==36);
    assert(edp.registers[0].offset==0xc7200 && edp.registers[0].value==0x80000008);
    assert(edp.registers[edp.count-1].offset==0x45404);
    for(auto offset:{0x60540U,0x60544U,0x60030U,0x60410U,0x60800U,0x60940U,0x68180U,0x68280U,0xc8250U,0x45504U,0x164290U})
        assert(panel.seen(offset));
    for(auto offset:panel.reads)assert(offset<0x61000 || offset>=0x64000);
    IO hdmi;hdmi.regs={{0x45404,0x1001},{0x61400,0x90000000}};
    const auto hdmiBoot=AlderLakeN::readDisplay(hdmi,true);hdmi.reads.clear();
    auto noPanel=AlderLakeP::readEDP(hdmi,hdmiBoot);
    assert(noPanel.status==EDPStatus::Complete && !noPanel.dpPipes && noPanel.count==23);
    assert(!hdmi.seen(0x61540) && !hdmi.seen(0x61800) && !hdmi.seen(0x68980) && !hdmi.seen(0x60540));
    IO dpB;dpB.regs={{0x45404,0x1001},{0x61400,0x92000000}};
    const auto dpBoot=AlderLakeN::readDisplay(dpB,true);dpB.reads.clear();
    auto external=AlderLakeP::readEDP(dpB,dpBoot);
    assert(external.status==EDPStatus::Complete && external.dpPipes==2 && !external.ddiAPipes && external.count==36);
    assert(dpB.seen(0x61540) && dpB.seen(0x68980) && !dpB.seen(0x60540) && !dpB.seen(0x68180));
    auto moved=eDP();moved.changePowerAt=3;
    const auto movedBoot=AlderLakeN::readDisplay(moved,true);
    assert(movedBoot.status==AlderLakeN::ReadoutStatus::Complete);
    assert(AlderLakeP::readEDP(moved,movedBoot).status==EDPStatus::PowerChanged);
    auto failed=eDP();failed.regs[0x45404]=UINT32_MAX;
    const auto failedBoot=AlderLakeN::readDisplay(failed,true);failed.reads.clear();
    assert(AlderLakeP::readEDP(failed,failedBoot).status==EDPStatus::Unavailable && failed.reads.empty());
    auto truncated=eDP();truncated.limit=0x164290;
    const auto truncatedBoot=AlderLakeN::readDisplay(truncated,true);
    assert(truncatedBoot.status==AlderLakeN::ReadoutStatus::Complete);
    auto partial=AlderLakeP::readEDP(truncated,truncatedBoot);
    assert(partial.status==EDPStatus::Unavailable && partial.dpPipes==1);
    std::puts("AlderLakeP eDP readout, DP SST selection, power-change and bounded-read fixtures passed");
}
