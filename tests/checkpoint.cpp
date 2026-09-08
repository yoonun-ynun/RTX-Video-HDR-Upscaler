#include "checkpoint.h"
#include <iostream>

static void Require(bool value,const char* message) {if(!value)throw Failure(5,message);}
int main() {
    try {
        auto root=std::filesystem::current_path()/L"artifacts";
        std::filesystem::create_directories(root);
        checkpoint::Job job;job.directory=CreateRunDirectory(root/L"checkpoint-test.mkv");
        job.input=root/L"한글 input.mp4";job.output=root/L"comparison.hdr.mkv";
        job.engine="test-engine";job.cq=21;job.parts.push_back({"part-000000.mkv","test-hash",300});
        for(bool compare:{false,true}) {
            job.compare=compare;job.Save();checkpoint::Job loaded;loaded.Load(job.directory/L"checkpoint.txt");
            Require(loaded.compare==compare && loaded.input==job.input && loaded.output==job.output && loaded.cq==21 && loaded.Count()==300,"Checkpoint lost comparison or job settings");
        }
        // A v1 manifest must reset a previously loaded comparison flag to normal HDR.
        std::string legacy="RTXHDR_CHECKPOINT_1\n\"input.mp4\" \"out.mkv\"\n0 0 18 0 10 \"\" 0 \"\"\n0 0 \"\" \"\" \"engine\"\n0 0 \"\" \"\" 0\n";
        checkpoint::Atomic(job.directory/L"checkpoint.txt",checkpoint::Digest(legacy)+"\n"+legacy);
        job.Load(job.directory/L"checkpoint.txt");Require(!job.compare && job.Count()==0,"Legacy checkpoint did not default to normal HDR");
        auto invalid=legacy;invalid.replace(invalid.find("CHECKPOINT_1"),12,"CHECKPOINT_2");
        checkpoint::Atomic(job.directory/L"checkpoint.txt",checkpoint::Digest(invalid)+"\n"+invalid);
        bool rejected=false;try {job.Load(job.directory/L"checkpoint.txt");}catch(const Failure&){rejected=true;}
        Require(rejected,"Missing comparison field was accepted");
        std::cout<<"Checkpoint v2 roundtrip, legacy defaults and malformed v2 rejected\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
