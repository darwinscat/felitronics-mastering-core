// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of felitronics-mastering-core — see LICENSE.
#include "../../../tests/DeclaredBudget.h"
#include "Driver.h"
#include <felitronics/session/Snapshot.h>
#include <felitronics/analysis/PeakExcursions.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace felitronics::session;
using Excursions = felitronics::analysis::PeakExcursions;
using felitronics::test::ok;
namespace declared = felitronics::declared;
namespace
{
constexpr auto slot = std::size_t (Analyzer::Excursions);
bool same (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
double number (const MeasurementResult& result, std::string_view name)
{
    for (const auto& v : result.numbers) if (v.name == name && v.value) return *v.value;
    return std::numeric_limits<double>::quiet_NaN();
}
void load (Session& s, const std::vector<float>& pcm, std::uint32_t rate, std::uint32_t channels)
{
    const float* input[] { pcm.data(), pcm.data() };
    ok (s.apply (command::Load { 1, { input, channels, pcm.size(), rate }, {} }).rejection == Rejection::None, "load source");
}
void readings (Session& s, double lufs = -18, double peak = 0)
{
    const auto snapshot = s.snapshot();
    MeasurementValue values[] { { "integratedLufs", lufs, MeasurementReason::None, 0 }, { "truePeakDb", peak, MeasurementReason::None, 0 } };
    MeasurementResult result; result.analyzer = Analyzer::Loudness; result.status = MeasurementStatus::Ready; result.reason = MeasurementReason::None;
    result.key = snapshot.view().measurements[0].key; result.framesRead = s.source().frames; result.complete = true; result.numbers = values;
    ok (detail::Driver::retain (s, s.measurementJob(), result), "controller retains input loudness and true peak before phase one ends");
}
void target (Session& s, double ceiling)
{
    // Input LUFS=-18, peak=0, target TP=-1: need=19+targetLUFS; ceiling=-need.
    command::EditTarget request; request.id = 2; request.fields.lufs = -19 - ceiling; request.fields.tp = -1;
    ok (s.apply (request).rejection == Rejection::None, "target request accepted");
}
void finish (Session& s, std::uint32_t budgetUnits = 7)
{
    while (s.needlesJob() != 0) (void) s.step (budgetUnits);
}
void compare (const MeasurementResult& r, const Excursions& a)
{
    const std::pair<std::string_view,double> fields[] {
        {"sampleRate",a.sampleRate()}, {"channels",double(a.channels())}, {"thresholdDbTp",a.params().thresholdDbtp},
        {"thresholdLinear",a.thresholdLinear()}, {"mergeMs",a.params().mergeMs}, {"samplesProcessed",double(a.samplesProcessed())},
        {"measuredOs",double(a.measuredOs())}, {"reason",double(a.reason())}, {"valid",a.valid()?1.0:0.0},
        {"reconstructedPeak",a.reconstructedPeak()}, {"samplePeakLinear",a.samplePeakLinear()}, {"truePeakLinear",a.truePeakLinear()},
        {"runCount",double(a.runCount())}, {"storedRunCount",double(a.storedRunCount())}, {"runsComplete",a.runsComplete()?1.0:0.0},
        {"aboveOs",double(a.aboveOs())}, {"occupancy",a.occupancy()}, {"totalDose",a.totalDose()}, {"maxExcess",a.maxExcess()},
        {"p90Ms",a.p90Ms()}, {"p90Saturated",a.p90Saturated()?1.0:0.0}, {"runsPerMinute",a.runsPerMinute()},
        {"nonFiniteSamples",double(a.nonFiniteSamples())}, {"firstNonFiniteAt",double(a.firstNonFiniteAt())},
        {"bassDoseShare",a.doseShareBelow(200)}, {"ceilingDensity",a.ceilingDensity()}, {"ceilingDensityAbove",a.ceilingDensityAbove(3,0.3)} };
    for (const auto& [name,value] : fields) ok (same (number(r,name),value), std::string("direct parity: ") + std::string(name));
    ok (r.status == MeasurementStatus::Ready && r.framesRead == std::uint64_t(a.samplesProcessed()), "owned result finished on the complete source");
    ok (r.arrays.size() == 5 && same(number(r,"runListIncluded"),0) && same(number(r,"aggregatesComplete"),1), "aggregate completeness and omitted run coordinates are explicit");
    bool equal = true;
    for (int i=0;i<Excursions::kDurationBins;++i) equal = equal && same(r.arrays[0].values[std::size_t(i)],double(a.durationBin(i)));
    for (int i=0;i<Excursions::kCeilingBins;++i) equal = equal && same(r.arrays[1].values[std::size_t(i)],double(a.ceilingBin(i)));
    for (int i=0;i<Excursions::kCrestBins;++i)
    {
        const auto at=std::size_t(3*i);
        equal = equal && same(r.arrays[2].values[at],Excursions::crestBinLowHz(i))
            && same(r.arrays[2].values[at+1],double(a.crestBinCount(i))) && same(r.arrays[2].values[at+2],a.crestBinDose(i));
    }
    for (int i=0;i<Excursions::kClasses;++i)
    {
        const auto at=std::size_t(3*i);
        equal = equal && same(r.arrays[3].values[at],i<Excursions::kClassEdges?a.params().classEdgesMs[i]:std::numeric_limits<double>::infinity())
            && same(r.arrays[3].values[at+1],double(a.classCount(i))) && same(r.arrays[3].values[at+2],a.classDose(i));
    }
    for (int i=0;i<a.channels();++i) equal=equal && same(r.arrays[4].values[std::size_t(i)],double(a.aboveOs(i)));
    ok (equal,"every aggregate histogram bin equals the direct analyzer");
}
std::vector<float> signal (std::size_t frames)
{
    std::vector<float> pcm(frames);
    for (std::size_t i=0;i<frames;++i)
        pcm[i]= i%2048<2 ? -0.95f : float(int(i%128)-64)/80.0f;
    return pcm;
}
void parity (bool fixture)
{
    for (const auto channels : {1u,2u}) for (const auto rate : {8000u,48000u,192000u})
    {
        auto pcm=signal(8199);
        auto created=Session::create(); auto& s=*created.session;
        load(s,pcm,rate,channels); target(s,-6); readings(s);
        ok(s.needlesJob()==0,"target waits for first measurement end");
        (void)detail::Driver::measured1(s,s.measurementJob(),s.source().hash);
        ok(s.needlesJob()!=0,"first measurement end schedules needles for the latest target");
        (void) detail::Driver::measured2 (s, s.measurementJob(), s.source().hash);
        for (double ceiling : {-6.0,-3.01,-12.75,-200.0})
        {
            target(s,ceiling);
            const auto demand=s.needlesStorage(ceiling);
            const auto before=s.snapshot();
            ok(demand.rejection==Rejection::None && before.view().needlesBytes==double(demand.bytes),"job demand is published before preparation");
            const auto prepared=declared::spend([&]{(void)s.step(1);});
            ok(declared::covers(demand.bytes,prepared),"declared job bytes cover preparation and MSVC debug proxies/padding");
            const auto processed=declared::spend([&]{finish(s);});
            ok(processed.bytes==0,"all reads, finish and ownership publication allocate zero");
            std::printf("needles %u Hz/%u ch: declared %llu allocated %lld, finish %lld\n",rate,channels,(unsigned long long)demand.bytes,prepared.bytes,processed.bytes);
            const auto saved=s.snapshot(); const auto& r=saved.view().measurements[slot];
            Excursions a; Excursions::Params p; p.thresholdDbtp=ceiling; a.setParams(p);
            ok(a.prepare(rate,int(channels)),"direct preparation");
            const float* input[]{pcm.data(),pcm.data()};
            ok(a.process(input,int(channels),int(pcm.size())) && a.finish(),"one-block direct run");
            compare(r,a);
            if(fixture)
            {
                std::printf("fixture %u %u %.17g %llu\n",rate,channels,ceiling,(unsigned long long)r.key);
                for(const auto& n:r.numbers) std::printf("%.*s %016llx\n",int(n.name.size()),n.name.data(),(unsigned long long)std::bit_cast<std::uint64_t>(*n.value));
                for(const auto& arr:r.arrays) for(double v:arr.values) std::printf("%016llx\n",(unsigned long long)std::bit_cast<std::uint64_t>(v));
            }
            if(fixture)
            {
                auto view=saved.view(); view.measurementStorage={}; view.needlesBytes=view.needlesLargestBlockBytes=0;
                const auto count=Codec::encodedBytes(view); std::string text(std::size_t(count.bytes),'\0');
                ok(Codec::encode(view,text)==CodecStatus::Ok,"cross-tier codec fixture");
                std::printf("codec %s\n",text.c_str());
            }
            Snapshot copied; const auto copyPrice=s.snapshotBytes();
            const auto copy=declared::spend([&]{copied=s.snapshot();});
            ok(std::uint64_t(copy.bytes)==copyPrice,"owned snapshot requests exactly its declared arrays and names");
            const auto size=Codec::encodedBytes(saved.view()); std::string json(std::size_t(size.bytes),'\0');
            ok(size.status==CodecStatus::Ok && Codec::encode(saved.view(),json)==CodecStatus::Ok,"needles result encodes");
            Snapshot decoded;
            const auto decodedPrice=Codec::decodedBytes(json);
            CodecStatus decodedStatus {};
            const auto decodedSpent=declared::spend([&]{decodedStatus=Codec::decode(json,decoded);});
            ok(decodedStatus==CodecStatus::Ok,"codec restores needles");
            ok(declared::covers(decodedPrice.bytes,decodedSpent),"codec allocation declaration covers owned output");
            compare(decoded.view().measurements[slot],a);
        }
    }
}
void lifecycle()
{
    auto pcm=signal(12001); auto created=Session::create(); auto& s=*created.session;
    load(s,pcm,48000,2); readings(s); target(s,-6); (void)detail::Driver::measured1(s,s.measurementJob(),s.source().hash);
    const auto first=s.needlesJob(); (void)s.step(3); const auto progress=s.snapshot().view().needlesProgress;
    ok(progress.completedUnits==3,"preparation and two chunks report progress");
    const auto source=s.source().hash;
    ok(s.apply(command::Cancel{3,first}).rejection==Rejection::None,"cancel needles mid-job");
    ok(s.source().hash==source && s.state()==State::Measured1 && s.needlesJob()==0
       && s.snapshot().view().measurements[slot].status==MeasurementStatus::Cancelled,"cancel preserves source and first-phase results");
    target(s,-6); const auto second=s.needlesJob(); (void)s.step(2); target(s,-9); const auto third=s.needlesJob();
    ok(first!=second && second!=third && s.snapshot().view().needlesProgress.completedUnits==0,"second target replaces partial work with a fresh job");
    ok(s.apply(command::Cancel{4,second}).rejection==Rejection::UnknownJob,"stale cancel cannot cancel latest request");
    finish(s,1); const auto saved=s.snapshot();
    ok(same(number(saved.view().measurements[slot],"thresholdDbTp"),-9),"latest ceiling alone is published");
    target(s,-3); ok(s.needlesJob()==0 && s.snapshot().view().measurements[slot].reason==MeasurementReason::NeedNotAbove3,"need exactly 3 skips");
    target(s,-2); ok(s.needlesJob()==0,"need below 3 skips");
    target(s,-3.01); ok(s.needlesJob()!=0,"need above 3 starts a new source pass"); (void)s.step(2);
    const auto old=s.needlesJob(); pcm[0]=0.1f; load(s,pcm,48000,2);
    ok(s.source().hash!=source && s.needlesJob()==0,"new source discards pending needles");
    ok(s.apply(command::Cancel{5,old}).rejection==Rejection::UnknownJob,"old source job is stale");
    readings(s); (void)detail::Driver::measured1(s,s.measurementJob(),s.source().hash); finish(s);
    ok(s.snapshot().view().needlesSource==s.source().hash,"replacement result is tied to new source");
    ok(saved.view().needlesSource==source && same(number(saved.view().measurements[slot],"thresholdDbTp"),-9),"saved snapshot survives target and source replacement");
    target(s,-10); const auto bytes=s.needlesStorage(-10); const auto revision=s.revision();
    (void)s.setCapacity({s.liveBytes()+double(bytes.bytes)-1,9007199254740991.0});
    const auto spent=declared::spend([&]{(void)s.step(1);});
    ok(spent.bytes==0 && s.needlesJob()==0 && s.source().channels==2,"memory refusal occurs before analyzer or output allocation");
    ok(s.events().size()==2 && s.events()[0].payload.error.code==ErrorCode::Memory
       && s.events()[0].payload.error.needBytes>s.capabilities().heapCeilingBytes
       && s.snapshot().view().measurements[slot].reason==MeasurementReason::Memory && s.revision()>revision,"memory event gives exact needBytes and an explicit unavailable result");
    (void)s.setCapacity({}); target(s,-10); (void)s.setCapacity({9007199254740991.0,double(bytes.largestBlockBytes)-1});
    const auto fragmented=declared::spend([&]{(void)s.step(1);});
    ok(fragmented.bytes==0 && s.events()[0].payload.error.code==ErrorCode::Memory,"largest-block refusal precedes preparation");
    (void)s.setCapacity({}); target(s,-201);
    ok(s.needlesJob()==0 && s.snapshot().view().measurements[slot].reason==MeasurementReason::Unsupported,"out-of-analyzer-domain ceiling is honest and does no work");
}
void measured2Cancellation()
{
    auto pcm = signal (12001); auto created = Session::create(); auto& s = *created.session;
    load (s, pcm, 48000, 2); readings (s); target (s, -6);
    (void) detail::Driver::measured1 (s, s.measurementJob(), s.source().hash);
    (void) detail::Driver::measured2 (s, s.measurementJob(), s.source().hash);
    while (s.needlesJob() != 0) (void) s.step (7);
    ok (s.state() == State::Measured2 && s.measurementJob() == 0, "both measurement phases completed");
    const auto saved = s.snapshot();
    const auto source = s.source().hash;
    target (s, -9); const auto job = s.needlesJob(); (void) s.step (2);
    ok (job != 0 && s.state() == State::Measured2, "target change starts needles after phase two");
    const auto revision = s.revision();
    ok (s.apply (command::Cancel { 3, job + 1 }).rejection == Rejection::UnknownJob
        && s.needlesJob() == job && s.revision() == revision, "wrong ID cannot cancel phase-two needles");
    ok (s.check (command::Cancel { 4, job }).rejection == Rejection::None, "phase-two needles cancellation preflight");
    Answer answer;
    const auto spent = declared::spend ([&] { answer = s.apply (command::Cancel { 4, job }); });
    const auto stopped = s.snapshot();
    ok (answer.rejection == Rejection::None && spent.bytes == 0 && s.needlesJob() == 0 && s.measurementJob() == 0
        && s.state() == State::Measured2 && s.source().hash == source, "cancel phase-two needles without allocation or source loss");
    ok (stopped.view().measurements[slot].status == MeasurementStatus::Cancelled
        && stopped.view().measurements[slot].reason == MeasurementReason::Cancelled
        && same (number (stopped.view().measurements[0], "integratedLufs"), -18)
        && same (number (saved.view().measurements[slot], "thresholdDbTp"), -6), "cancellation preserves completed source readings and saved needles");
    ok (s.apply (command::Cancel { 5, job }).rejection == Rejection::NoJob, "no active phase-two job still returns NoJob");
    target (s, -12); const auto next = s.needlesJob();
    ok (next != 0 && next != job && s.apply (command::Cancel { 6, job }).rejection == Rejection::UnknownJob,
        "cancelled phase-two ID cannot cancel its replacement");
    finish (s);
    ok (same (number (s.snapshot().view().measurements[slot], "thresholdDbTp"), -12), "replacement completes after phase-two cancellation");
}
// The real pump starts needles at the first phase's gate, one unit BEFORE Measured1. A measurement cancelled right there
// stands in Stopped with the needles job still running: that job is cancellable in Stopped as in every other column.
void cancelWhileStopped()
{
    std::vector<float> pcm (48000);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = i % 2048 < 2 ? -0.95f : float (int (i % 128) - 64) / 8000.0f;
    auto created = Session::create(); auto& s = *created.session;
    load (s, pcm, 48000, 2);
    for (unsigned units = 0; units < 100000 && s.needlesJob() == 0 && s.measurementJob() != 0; ++units) (void) s.step (1);
    const auto needles = s.needlesJob();
    ok (needles != 0 && s.state() == State::Loaded, "needles start at the gate while the first measurement is still Loaded");
    ok (s.apply (command::Cancel { 2, s.measurementJob() }).rejection == Rejection::None && s.column() == Column::Stopped
        && s.needlesJob() == needles, "cancelling the measurement there stops it and leaves the needles job running");
    const auto revision = s.revision();
    const auto answer = s.apply (command::Cancel { 3, needles });
    ok (answer.rejection == Rejection::None && answer.revision == revision + 1 && s.needlesJob() == 0 && s.column() == Column::Stopped,
        "the running needles job is cancellable in Stopped");
    ok (s.events().size() == 1 && s.events()[0].kind == EventKind::Fact && s.events()[0].payload.fact.view().id == text::FactId::Cancelled,
        "its fact says cancelled, not the measurement stopped again");
    ok (s.snapshot().view().measurements[slot].status == MeasurementStatus::Cancelled && s.step (1).state == StepState::Done,
        "nothing steps on after the cancel");
    ok (s.apply (command::Cancel { 4, needles }).rejection == Rejection::NoJob, "with no job left, NoJob as in every column");
    ok (s.apply (command::ContinueMeasurement { 5 }).rejection == Rejection::None, "the stopped measurement continues");
    for (unsigned units = 0; units < 100000 && s.measurementJob() != 0; ++units) (void) s.step (7);
    ok (s.state() == State::Measured2 && s.snapshot().view().measurements[slot].status == MeasurementStatus::Cancelled,
        "the measurement completes; the cancelled needles stay cancelled until a target asks again");
}
// Unusable readings (silence has no finite loudness) schedule nothing, and the result says why — the loudness result's
// reason — instead of Pending with no job behind it; a reload of the same source answers the same.
void unusableReadings()
{
    const std::vector<float> pcm (48000, 0.0f);
    auto created = Session::create(); auto& s = *created.session;
    for (const bool reload : { false, true })
    {
        load (s, pcm, 48000, 2);
        for (unsigned units = 0; units < 100000 && s.measurementJob() != 0; ++units) (void) s.step (7);
        const auto ended = s.snapshot();
        const auto& r = ended.view().measurements[slot];
        ok (s.measurementJob() == 0 && s.needlesJob() == 0 && ! ended.view().mandatoryMeasurementsReady,
            reload ? "a cached reload of silence starts no needles" : "silence ends its measurement without needles");
        ok (r.status == MeasurementStatus::Unavailable && r.reason == MeasurementReason::NoSignal
            && ended.view().measurements[0].reason == MeasurementReason::NoSignal,
            "needles are unavailable with the loudness result's reason, never pending without a job");
    }
}
void dense()
{
    std::vector<float> pcm(128u*65540u,0.0f);
    for(std::size_t i=0;i<pcm.size();i+=128) pcm[i]=pcm[i+1]=0.95f;
    auto created=Session::create();auto& s=*created.session;load(s,pcm,48000,2);readings(s);target(s,-6);(void)detail::Driver::measured1(s,s.measurementJob(),s.source().hash);
    finish(s,32);const auto snap=s.snapshot();const auto& r=snap.view().measurements[slot];
    ok(number(r,"runCount")>65536 && same(number(r,"storedRunCount"),65536) && snap.view().needlesRunsTruncated,"dense run exhaustion is explicit in snapshot");
    Excursions a;Excursions::Params p;p.thresholdDbtp=-6;a.setParams(p);ok(a.prepare(48000,2),"dense direct prep");
    const float* input[]{pcm.data(),pcm.data()};ok(a.process(input,2,int(pcm.size())) && a.finish(),"dense direct finish");compare(r,a);
    std::printf("dense: %.0f runs, %.0f stored, aggregate parity after truncation\n",number(r,"runCount"),number(r,"storedRunCount"));
}
}
int main(int argc,char** argv)
{
    std::setbuf(stdout,nullptr);
    const bool fixture=argc>1 && std::string_view(argv[1])=="--fixture";
    if(argc>1 && std::string_view(argv[1])=="--dense") dense(); else {parity(fixture);lifecycle();measured2Cancellation();cancelWhileStopped();unusableReadings();}
    return felitronics::test::report();
}
