#pragma once

#include "lib/data/Words.h"
#include <opencv2/flann.hpp>
#include <memory>

class WordSearch final {
public:
  explicit WordSearch(const std::map<int, Word> &words);

  std::vector<int> search(const cv::Mat &descriptors) const;

private:
  void buildIndex();

private:
  const std::map<int, Word> &_words;
  int _type;
  int _dim;
  std::map<int, std::map<int, std::shared_ptr<Word>>> _wordsByDb;
  cv::Mat _dataMat;
  std::unique_ptr<cv::flann::Index> _index;
  std::map<int, int> _mapIndexId; // (row num, word id)
};
