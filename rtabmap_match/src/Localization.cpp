#include <rtabmap/core/Memory.h>
#include <rtabmap/core/Signature.h>
#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/utilite/UConversion.h>
#include <rtabmap/utilite/UStl.h>
#include <rtabmap/utilite/UMath.h>
#include <rtabmap/core/VWDictionary.h>
#include <rtabmap/core/Rtabmap.h>
#include <rtabmap/core/Optimizer.h>
#include <QCoreApplication>

#include "Localization.h"
#include "ImageEvent.h"
#include "LocationEvent.h"

Localization::Localization(const std::string dbPath, const rtabmap::ParametersMap &parameters) :
    _dbPath(dbPath),
    _topk(TOP_K)
{
    // Setup memory
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemRehearsalSimilarity(), "1.0")); // desactivate rehearsal
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemBinDataKept(), "false"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemImageKept(), "true"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemSTMSize(), "0"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemNotLinkedNodesKept(), "false"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpTfIdfLikelihoodUsed(), "false"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpDetectorStrategy(), uNumber2Str(rtabmap::Feature2D::kFeatureSurf)));
    // parameters that makes memory do PnP localization for RGB images
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisEstimationType(), "1")); // Motion estimation approach: 0:3D->3D, 1:3D->2D (PnP), 2:2D->2D (Epipolar Geometry)
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemIncrementalMemory(), "false"));
    _memoryParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisMinInliers(), "20"));

    _memory = new MemoryLoc();
    if (!_memory || !_memory->init(_dbPath, false, _memoryParams))
    {
        UERROR("Error initializing the memory for Localization.");
    }

    optimizeGraph();

    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemIncrementalMemory(), "true")); // make sure it is incremental
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemRehearsalSimilarity(), "1.0")); // desactivate rehearsal
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemBinDataKept(), "false"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemSTMSize(), "0"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpIncrementalDictionary(), "true")); // make sure it is incremental
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpNewWordsComparedTogether(), "false"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpNNStrategy(), uNumber2Str(rtabmap::VWDictionary::kNNBruteForce))); // bruteforce
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpNndrRatio(), "0.3"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpDetectorStrategy(), uNumber2Str(rtabmap::Feature2D::kFeatureSurf)));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpMaxFeatures(), "1500"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpBadSignRatio(), "0"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kKpRoiRatios(), "0.0 0.0 0.0 0.0"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kMemGenerateIds(), "false"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisMinInliers(), "4"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisIterations(), "2000"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisPnPReprojError(), "1.0"));
    _memoryLocParams.insert(rtabmap::ParametersPair(rtabmap::Parameters::kVisPnPFlags(), "0")); // 0=Iterative, 1=EPNP, 2=P3P
}

Localization::~Localization()
{
    delete _memory;
}

void Localization::setVisibility(Visibility *vis)
{
    _vis = vis;
}

bool Localization::event(QEvent *event)
{
    if (event->type() == ImageEvent::type())
    {
        ImageEvent *imageEvent = static_cast<ImageEvent *>(event);
        rtabmap::Transform pose = localize(imageEvent->sensorData());
        // a null pose notify that loc could not be computed
        if (!pose.isNull())
        {
            QCoreApplication::postEvent(_vis, new LocationEvent(imageEvent->sensorData(), pose, imageEvent->conInfo()));
        }
        // TODO post failure event to httpserver
        return true;
    }
    return QObject::event(event);
}

rtabmap::Transform Localization::localize(rtabmap::SensorData *sensorData)
{
    UASSERT(!sensorData->imageRaw().empty());

    rtabmap::Transform output;

    const rtabmap::CameraModel &cameraModel = sensorData->cameraModels()[0];

    if (_memory->getWorkingMem().size() >= 1)
    {
        // generate kpts
        if (_memory->update(*sensorData))
        {
            UDEBUG("");
            const rtabmap::Signature *newS = _memory->getLastWorkingSignature();
            UDEBUG("newWords=%d", (int)newS->getWords().size());
            int minInliers;
            rtabmap::Parameters::parse(_memoryLocParams, rtabmap::Parameters::kVisMinInliers(), minInliers);
            if ((int)newS->getWords().size() > minInliers)
            {
                std::map<int, float> likelihood;
                std::list<int> signaturesToCompare = uKeysList(_memory->getWorkingMem());
                UDEBUG("signaturesToCompare.size() = %d", signaturesToCompare.size());
                likelihood = _memory->computeLikelihood(newS, signaturesToCompare);

                rtabmap::Rtabmap rtabmap;
                rtabmap.adjustLikelihood(likelihood);

                std::vector<int> topIds;
                likelihood.erase(-1);
                int topId;
                if (likelihood.size())
                {
                    std::vector< std::pair<int, float> > top(_topk);
                    std::partial_sort_copy(likelihood.begin(),
                                           likelihood.end(),
                                           top.begin(),
                                           top.end(),
                                           compareLikelihood);
                    // TODO there is some bugs here
                    for (std::vector< std::pair<int, float> >::iterator it = top.begin(); it != top.end(); ++it)
                    {
                        topIds.push_back(it->first);
                    }
                    topId = topIds[0];
                    UINFO("topId: %d", topId);
                }

                MemoryLoc memoryLoc(_memoryLocParams);

                std::string rejectedMsg;
                int visualInliers = 0;
                double variance = 1;
                bool success = true;
                sensorData->setId(newS->id());
                if (sensorData->id() == rtabmap::Memory::kIdInvalid)
                {
                    success = false;
                }

                if (success)
                {
                    std::vector<int> sortedIds = topIds;
                    std::sort(sortedIds.begin(), sortedIds.end());
                    for (std::vector<int>::const_iterator it = sortedIds.begin(); it != sortedIds.end(); ++it)
                    {
                        rtabmap::SensorData data = _memory->getNodeData(*it, true);
                        const rtabmap::Signature *sig = _memory->getSignature(*it);

                        if (!data.depthOrRightRaw().empty() && data.id() != rtabmap::Memory::kIdInvalid && sig != NULL)
                        {
                            UDEBUG("Calculate map transform with raw data");
                            memoryLoc.update(data, getPose(sig), sig->getPoseCovariance());
                        }
                        else
                        {
                            UWARN("Data incomplete. data.depthOrRightRaw().empty() = %d, data.id() = %d", data.depthOrRightRaw().empty(), data.id());
                            success = false;
                            break;
                        }
                    }

                    memoryLoc.update(*sensorData);
                }

                if (success)
                {
                    output = memoryLoc.computeGlobalVisualTransform(topIds, sensorData->id(), &rejectedMsg, &visualInliers, &variance);

                    if (!output.isNull())
                    {
                        UDEBUG("global transform = %s", output.prettyPrint().c_str());
                    }
                    else
                    {
                        UWARN("transform is null, rejectMsg = %s, using pose of the closest image", rejectedMsg.c_str());
                        output = getPose(_memory->getSignature(topId));
                    }
                }
            }
            else
            {
                UWARN("new signature doesn't have enough words. newWords=%d ", (int)newS->getWords().size());
            }

            // remove new words from dictionary
            _memory->deleteLocation(newS->id());
            _memory->emptyTrash();
        }
    }
    else
    {
        UERROR("Memory not initialized. _memory->getWorkingMem().size() = %d", _memory->getWorkingMem().size());
    }

    UINFO("output transform = %s", output.prettyPrint().c_str());

    return output;
}

void Localization::optimizeGraph()
{
    // get the graph
    std::list<int> idList = uKeysList(_memory->getWorkingMem());
    std::set<int> idSet(idList.begin(), idList.end());
    std::map<int, rtabmap::Transform> poses;
    std::multimap<int, rtabmap::Link> links;
    bool lookInDatabase = true;
    _memory->getMetricConstraints(idSet, poses, links, lookInDatabase);

    //optimize the graph
    rtabmap::Optimizer::Type optimizerType = rtabmap::Optimizer::kTypeTORO; // options: kTypeTORO, kTypeG2O, kTypeGTSAM, kTypeCVSBA
    rtabmap::Optimizer *graphOptimizer = rtabmap::Optimizer::create(optimizerType);
    _optimizedPoses = graphOptimizer->optimize(poses.begin()->first, poses, links);
}

rtabmap::Transform Localization::getPose(const rtabmap::Signature *sig) const
{
    if (sig == NULL)
    {
        return rtabmap::Transform();
    }
    rtabmap::Transform pose = sig->getPose();
    const std::map<int, rtabmap::Transform>::const_iterator poseIter = _optimizedPoses.find(sig->id());
    if (poseIter != _optimizedPoses.end())
    {
        pose = poseIter->second;
    }

    return pose;
}

bool Localization::compareLikelihood(std::pair<const int, float> const &l, std::pair<const int, float> const &r)
{
    return l.second > r.second;
}
