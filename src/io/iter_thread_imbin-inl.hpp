#ifndef ITER_THREAD_IMBIN_INL_HPP
#define ITER_THREAD_IMBIN_INL_HPP
#pragma once
/*!
 * \file cxxnet_iter_thread_imbin-inl.hpp
 * \brief threaded version of page iterator
 * \author Tianqi Chen
 */
#include "data.h"
#include <opencv2/opencv.hpp>
#include "../utils/thread_buffer.h"
#include "../utils/utils.h"

namespace cxxnet {
/*! \brief thread buffer iterator */
class ThreadImagePageIterator: public IIterator< DataInst > {
public:
  ThreadImagePageIterator(void) {
    idx_ = 0;
    img_.set_pad(false);
    fplst_ = NULL;
    silent_ = 0;
    itr.SetParam("buffer_size", "4");
    page_.page = NULL;
  }
  virtual ~ThreadImagePageIterator(void) {
    if (fplst_ != NULL) fclose(fplst_);
  }
  virtual void SetParam(const char *name, const char *val) {
    if (!strcmp(name, "image_list")) {
      raw_imglst_ += val;
      int bias = 0;
      char buf[1024];
      while(sscanf(val + bias, "%[^%,],", buf) == 1 && bias < raw_imglst_.size()) {
        printf("Set List: %s\n", buf);
        std::string v(buf);
        path_imglst_.push_back(v);
        bias += v.size() + 1;
      }
    }
    if (!strcmp(name, "image_bin")) {
      raw_imgbin_ += val;
      int bias = 0;
      char buf[1024];
      while(sscanf(val + bias, "%[^%,],", buf) == 1 && bias < raw_imgbin_.size()) {
        printf("Set Bin:%s\n", buf);
        std::string v(buf);
        path_imgbin_.push_back(v);
        bias += v.size() + 1;
      }
    }
    if (!strcmp(name, "silent"))      silent_ = atoi(val);
  }
  virtual void Init(void) {
    fplst_  = utils::FopenCheck(path_imglst_[0].c_str(), "r");
    if (silent_ == 0) {
      printf("ThreadImagePageIterator:image_list=%s, bin=%s\n", raw_imglst_.c_str(), raw_imgbin_.c_str());
    }
    utils::Check(path_imgbin_.size() == path_imglst_.size(), "List/Bin number not consist");
    itr.get_factory().path_imgbin = path_imgbin_;
    itr.get_factory().Ready();
    itr.Init();
    this->BeforeFirst();
  }
  virtual void BeforeFirst(void) {
    fseek(fplst_ , 0, SEEK_SET);
    itr.BeforeFirst();
    this->LoadNextPage();
  }
  virtual bool Next(void) {
    while (fscanf(fplst_, "%u%f%*[^\n]\n", &out_.index, &out_.label) == 2) {
      this->NextBuffer(buf_);
      this->LoadImage(img_, out_, buf_);
      return true;
    }
    idx_ += 1;
    idx_ %= path_imglst_.size();
    if (idx_ == 0 || path_imglst_.size() == 1) return false;
    else {
      if (fplst_) fclose(fplst_);
      fplst_  = utils::FopenCheck(path_imglst_[idx_].c_str(), "r");
      return Next();
    }
  }
  virtual const DataInst &Value(void) const {
    return out_;
  }
protected:
  inline static void LoadImage(mshadow::TensorContainer<cpu, 3> &img,
                               DataInst &out,
                               std::vector<unsigned char> &buf) {
    cv::Mat res = cv::imdecode(buf, 1);
    utils::Assert(res.data != NULL, "decoding fail");

    img.Resize(mshadow::Shape3(3, res.rows, res.cols));
    for (index_t y = 0; y < img.size(1); ++y) {
      for (index_t x = 0; x < img.size(2); ++x) {
        cv::Vec3b bgr = res.at<cv::Vec3b>(y, x);
        // store in RGB order
        img[2][y][x] = bgr[0];
        img[1][y][x] = bgr[1];
        img[0][y][x] = bgr[2];
      }
    }
    out.data = img;
    // free memory
    res.release();
  }
  inline void NextBuffer(std::vector<unsigned char> &buf) {
    while (ptop_ >= page_.page->Size()) {
      this->LoadNextPage();
    }
    utils::BinaryPage::Obj obj = (*page_.page)[ ptop_ ];
    buf.resize(obj.sz);
    memcpy(&buf[0], obj.dptr, obj.sz);
    ++ ptop_;
  }
  inline void LoadNextPage(void) {
    utils::Assert(itr.Next(page_), "can not get first page");
    ptop_ = 0;
  }
protected:
  int idx_;
  // output data
  DataInst out_;
  // silent
  int silent_;
  // file pointer to list file, information file
  FILE *fplst_;
  // prefix path of image binary, path to input lst, format: imageid label path
  std::vector<std::string> path_imgbin_, path_imglst_;
  std::string raw_imglst_, raw_imgbin_;
  // temp storage for image
  mshadow::TensorContainer<cpu, 3> img_;
  // temp memory buffer
  std::vector<unsigned char> buf_;
private:
  struct PagePtr {
    utils::BinaryPage *page;
  };
  struct Factory {
  public:
    utils::StdFile fi;
    std::vector<std::string> path_imgbin;
    int idx;
  public:
    Factory() : idx(0) {}
    inline bool Init() {
      return true;
    }
    inline void SetParam(const char *name, const char *val) {}
    inline void Ready() {
      fi.Open(path_imgbin[idx].c_str(), "rb");
    }
    inline bool LoadNext(PagePtr &val) {
      bool res = val.page->Load(fi);
      if (res) return res;
      else {
        idx += 1;
        idx %= path_imgbin.size();
        if (idx == 0) return false;
        else {
          fi.Close();
          fi.Open(path_imgbin[idx].c_str(), "rb");
          return val.page->Load(fi);
        }
      }
    }
    inline PagePtr Create(void) {
      PagePtr a; a.page = new utils::BinaryPage();
      return a;
    }
    inline void FreeSpace(PagePtr &a) {
      delete a.page;
    }
    inline void Destroy() {
    }
    inline void BeforeFirst() {
      fi.Seek(0);
    }
  };
protected:
  PagePtr page_;
  int     ptop_;
  utils::ThreadBuffer<PagePtr, Factory> itr;
}; // class ThreadImagePageIterator
}; // namespace cxxnet
#endif
