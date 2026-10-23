#pragma once
#include <algorithm>
#include <utility>
#include <memory>
#include <new>
namespace hp {
// Retain only the globally smallest prefix needed by the requested page.
// Scan order never changes page ordering; RAM is O(offset + page size), not
// O(directory size). Caller enforces the offset ceiling and cancellation.
template<class T,class Compare> class SortedPage {
    size_t limit_,count_=0;Compare compare_;std::unique_ptr<T[]> owned_;T* values_=nullptr;
public:
    SortedPage(size_t limit,Compare compare):limit_(limit),compare_(compare),owned_(new(std::nothrow) T[limit]),values_(owned_.get()){}
    SortedPage(T* storage,size_t limit,Compare compare):limit_(limit),compare_(compare),values_(storage){}
    bool valid()const{return !limit_||values_;}
    void add(T value){
        if(!limit_||!values_)return;
        if(count_<limit_){values_[count_++]=std::move(value);std::push_heap(values_,values_+count_,compare_);}
        else if(compare_(value,values_[0])){
            std::pop_heap(values_,values_+count_,compare_);values_[count_-1]=std::move(value);
            std::push_heap(values_,values_+count_,compare_);
        }
    }
    struct View {T* values;size_t count;T& operator[](size_t i){return values[i];}size_t size()const{return count;}bool empty()const{return !count;}};
    View sorted(){if(count_)std::sort_heap(values_,values_+count_,compare_);return {values_,count_};}
    size_t retained()const{return count_;}
};
}
