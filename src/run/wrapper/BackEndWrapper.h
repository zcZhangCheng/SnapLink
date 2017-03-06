#pragma once

#include "lib/algo/Feature.h"
#include "lib/algo/Perspective.h"
#include "lib/algo/Visibility.h"
#include "lib/algo/WordSearch.h"
#include "lib/algo/DbSearch.h"
#include <QEvent>
#include <QObject>


class BWFrontEndObj;
class HTTPFrontEndObj;
class CameraModel;
class Session;

class BackEndWrapper final : public QObject {
public:
  explicit BackEndWrapper(const std::shared_ptr<Words> &words,
                 std::unique_ptr<Labels> &&labels, int sampleSize, int corrSize, double distRatio);

  bool identify(const cv::Mat &image, const CameraModel &camera,
                std::vector<std::string> &results, Session &session);

protected:
  bool event(QEvent *event);

private:
  Feature _feature;
  WordSearch _wordSearch;
  DbSearch _dbSearch;
  Perspective _perspective;
  Visibility _visibility;
};
