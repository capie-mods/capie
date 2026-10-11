#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// Copied diagnostics only. No engine addresses, callbacks, or borrowed strings.
namespace rn::debug {
constexpr std::size_t ring_capacity=256, line_capacity=4096;
struct Stamp { std::uint64_t sequence{},ms{}; DWORD thread{}; bool truncated{}; };
struct Entry { Stamp stamp{}; std::size_t size{}; char text[line_capacity]{}; };
struct Dump {
    std::array<Entry,ring_capacity> entries{};
    std::size_t count{};
    std::uint64_t overwritten{};
    bool sequence_saturated{};
};
class Ring {
    SRWLOCK lock_=SRWLOCK_INIT;
    std::array<Entry,ring_capacity> entries_{};
    std::size_t start_{},count_{};
    std::uint64_t sequence_{},overwritten_{};
    bool armed_=true;
public:
    void arm(bool enabled) noexcept {
        AcquireSRWLockExclusive(&lock_); armed_=enabled; ReleaseSRWLockExclusive(&lock_);
    }
    bool append(const char* text,std::size_t size,std::uint64_t ms,DWORD thread,Stamp& stamp,bool force=false) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(!armed_ && !force) { ReleaseSRWLockExclusive(&lock_); return false; }
        if(sequence_!=(std::numeric_limits<std::uint64_t>::max)()) ++sequence_;
        stamp={sequence_,ms,thread,size>=line_capacity};
        std::size_t slot=(start_+count_)%ring_capacity;
        if(count_==ring_capacity) {
            slot=start_; start_=(start_+1)%ring_capacity;
            if(overwritten_!=(std::numeric_limits<std::uint64_t>::max)()) ++overwritten_;
        } else ++count_;
        auto& entry=entries_[slot]; entry={}; entry.stamp=stamp;
        entry.size=size<line_capacity?size:line_capacity-1;
        if(entry.size) std::memcpy(entry.text,text,entry.size);
        ReleaseSRWLockExclusive(&lock_); return true;
    }
    void copy(Dump& dump) noexcept {
        // One bounded copy under the lock. File I/O is always outside it.
        AcquireSRWLockShared(&lock_);
        dump.count=count_; dump.overwritten=overwritten_;
        dump.sequence_saturated=sequence_==(std::numeric_limits<std::uint64_t>::max)();
        for(std::size_t i=0;i<count_;++i) dump.entries[i]=entries_[(start_+i)%ring_capacity];
        ReleaseSRWLockShared(&lock_);
    }
    Ring()=default;
    Ring(const Ring&)=delete;
    Ring& operator=(const Ring&)=delete;
};

enum class Operation : std::uintptr_t { snapshot=1,dump=2,pause=3,resume=4 };
constexpr unsigned all_models=7;
constexpr std::uint64_t snapshot_ttl_ms=10000;
struct Work {
    std::uint64_t serial{};
    Operation operation{};
    unsigned pending{},expired{};
    bool changed{},snapshot{},dump{};
};
class Control {
    SRWLOCK lock_=SRWLOCK_INIT;
    std::uint64_t serial_{},reported_{},deadline_{};
    Operation operation_{};
    unsigned pending_{};
    bool dump_{};
public:
    // Remote thread may call only this scalar operation; never reads the GUI.
    bool request(Operation operation,std::uint64_t now) noexcept {
        const auto code=static_cast<std::uintptr_t>(operation);
        if(code<1 || code>4) return false;
        AcquireSRWLockExclusive(&lock_);
        if(serial_==(std::numeric_limits<std::uint64_t>::max)()) {
            ReleaseSRWLockExclusive(&lock_); return false;
        }
        ++serial_; operation_=operation;
        if(operation==Operation::snapshot) {
            pending_=all_models;
            deadline_=now>(std::numeric_limits<std::uint64_t>::max)()-snapshot_ttl_ms
                ?(std::numeric_limits<std::uint64_t>::max)():now+snapshot_ttl_ms;
            dump_=true;
        } else if(operation==Operation::dump) dump_=true;
        else if(operation==Operation::pause) { pending_=0; dump_=true; }
        ReleaseSRWLockExclusive(&lock_); return true;
    }
    Work take(std::uint64_t now,unsigned model_bit) noexcept {
        Work work{};
        AcquireSRWLockExclusive(&lock_);
        work.serial=serial_; work.operation=operation_;
        work.changed=serial_!=reported_; reported_=serial_;
        if(pending_ && now>=deadline_) { work.expired=pending_; pending_=0; }
        // Exactly one recognized model at a time; unrelated ticks use zero.
        if(model_bit && (model_bit&(model_bit-1))==0 && (model_bit&all_models)==model_bit && (pending_&model_bit)) {
            pending_&=~model_bit; work.snapshot=true;
        }
        work.pending=pending_;
        if(dump_ && !pending_) { dump_=false; work.dump=true; }
        ReleaseSRWLockExclusive(&lock_); return work;
    }
    Control()=default;
    Control(const Control&)=delete;
    Control& operator=(const Control&)=delete;
};
} // namespace rn::debug
