#pragma once

#include <rtabmap/core/Odometry.h>
#include <rtabmap/core/Transform.h>
#include <rtabmap/core/SensorData.h>
#include <rtabmap/core/Parameters.h>
#include <QObject>
#include <QEvent>
#include "MemoryLoc.h"
#include "Visibility.h"

#define TOP_K 2

class Visibility;

class Localization :
    public QObject
{
public:
    Localization(const std::string dbPath, const rtabmap::ParametersMap &parameters = rtabmap::ParametersMap());
    virtual ~Localization();

    void setVisibility(Visibility *vis);

protected:
    virtual bool event(QEvent *event);

private:
    rtabmap::Transform localize(rtabmap::SensorData *sensorData);
    void optimizeGraph();  // optimize poses using TORO graph
    // get pose from optimizedPoses if available, otherwise get from sig itself
    rtabmap::Transform getPose(const rtabmap::Signature *sig) const;
    static bool compareLikelihood(std::pair<const int, float> const &l, std::pair<const int, float> const &r);

private:
    int _topk;
    std::string _dbPath;
    MemoryLoc *_memory;
    std::map<int, rtabmap::Transform> _optimizedPoses;
    rtabmap::ParametersMap _memoryParams;
    rtabmap::ParametersMap _memoryLocParams;
    Visibility *_vis;
};
