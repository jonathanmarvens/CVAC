/******************************************************************************
 * CVAC Software Disclaimer
 * 
 * This software was developed at the Naval Postgraduate School, Monterey, CA,
 * by employees of the Federal Government in the course of their official duties.
 * Pursuant to title 17 Section 105 of the United States Code this software
 * is not subject to copyright protection and is in the public domain. It is 
 * an experimental system.  The Naval Postgraduate School assumes no
 * responsibility whatsoever for its use by other parties, and makes
 * no guarantees, expressed or implied, about its quality, reliability, 
 * or any other characteristic.
 * We would appreciate acknowledgement and a brief notification if the software
 * is used.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above notice,
 *       this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Naval Postgraduate School, nor the name of
 *       the U.S. Government, nor the names of its contributors may be used
 *       to endorse or promote products derived from this software without
 *       specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE NAVAL POSTGRADUATE SCHOOL (NPS) AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NPS OR THE U.S. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
#include "BowICETrainI.h"
#include <iostream>
#include <vector>
#include <ctime>  //for seeding the function "rand()"

#include <Ice/Communicator.h>
#include <Ice/Initialize.h>
#include <Ice/ObjectAdapter.h>
#include <util/processRunSet.h>
#include <util/FileUtils.h>
#include <util/ServiceManI.h>
#include <util/DetectorDataArchive.h>

using namespace Ice;
using namespace cvac;

///////////////////////////////////////////////////////////////////////////////
// This is called by IceBox to get the service to communicate with.
extern "C"
{
	//
	// ServiceManager handles all the icebox interactions so we construct
    // it and set a pointer to our detector.
	//
	ICE_DECLSPEC_EXPORT IceBox::Service* create(Ice::CommunicatorPtr communicator)
	{
        
        BowICETrainI *bow = new BowICETrainI();
        ServiceManagerI *sMan = new ServiceManagerI(bow, bow);
        bow->setServiceManager(sMan);
        return sMan;

	}
}

BowICETrainI::BowICETrainI()
:mServiceMan(NULL)
{    
  callbackPtr = NULL;
}

BowICETrainI::~BowICETrainI()
{
}

void BowICETrainI:: setServiceManager(cvac::ServiceManagerI *sman)
{
    mServiceMan = sman;
}

void BowICETrainI::starting()
{
    // Do anything needed on service starting
}

void BowICETrainI::stopping()
{
    // stop the training service
    mServiceMan->stopService();
}

bowCV* BowICETrainI::initialize( TrainerCallbackHandlerPrx& _callback,
                                 const TrainerProperties &tprops,
                                 DetectorDataArchive& dda,
                                 const Current& current )
{
  // Set CVAC verbosity according to ICE properties
  PropertiesPtr iceprops =
    (current.adapter->getCommunicator()->getProperties());
  string verbStr = iceprops->getProperty("CVAC.ServicesVerbosity");
  if (!verbStr.empty())
  {
    vLogger.setLocalVerbosityLevel( verbStr );
  }

  // defaults
  //SURF, SIFT, FAST, STAR, MSER, GFTT, HARRIS
  string	_nameFeature("SIFT");
  //SURF, SIFT, OpponentSIFT, OpponentSURF
  string	_nameDescriptor("SIFT");
  //BruteForce-L1, BruteForce, FlannBased  
  string	_nameMatcher("BruteForce-L1");
  int		_countWords = 150;	

  // read properties; need to cast const away
  cvac::Properties& trp = (cvac::Properties&) tprops.props;
  const string& nf = trp[bowCV::BOW_DETECTOR_NAME];
  if (!nf.empty())
  {
    _nameFeature = nf;
    localAndClientMsg(VLogger::DEBUG, _callback,
                      "Set FeatureType to %s\n", nf.c_str() );
  }
  const string& nd = trp[bowCV::BOW_EXTRACTOR_NAME];
  if (!nd.empty())
  {
    _nameDescriptor = nd;
    localAndClientMsg(VLogger::DEBUG, _callback,
                      "Set DescriptorType to %s\n", nd.c_str() );
  }
  const string& nm = trp[bowCV::BOW_MATCHER_NAME];
  if (!nm.empty())
  {
    _nameMatcher = nm;
    localAndClientMsg(VLogger::DEBUG, _callback,
                      "Set MatcherType to %s\n", nm.c_str() );
  }
  const string& nw = trp["NumWords"];
  if (!nw.empty())
  {
    errno=0;
    long int cw = (int) strtol( nw.c_str(), (char**) NULL, 10 );
    if (cw>0 && cw<INT_MAX && errno==0)
    {
      // no error, successfully parsed int from NumWords property
      _countWords = cw;
      localAndClientMsg(VLogger::DEBUG, _callback,
                        "Number of words set to %d\n", cw );
    }
  }

  // figure out how to handle Negative purpose samples, if any
  const string& strategy = trp[bowCV::BOW_REJECT_CLASS_STRATEGY];
  if (!strategy.empty())
  {
    std::string strat = strategy;
    std::transform( strategy.begin(), strategy.end(), strat.begin(), ::tolower );
    rejectClassStrategy = bowCV::BOW_REJECT_CLASS_AS_MULTICLASS;
    if (0==strat.compare(bowCV::BOW_REJECT_CLASS_IGNORE_SAMPLES))
    {
      rejectClassStrategy = bowCV::BOW_REJECT_CLASS_IGNORE_SAMPLES;
      localAndClientMsg(VLogger::DEBUG, _callback,
                        "BOW will ignore any samples with Negative purpose\n");
    }
    else if (0==strat.compare(bowCV::BOW_REJECT_CLASS_AS_MULTICLASS))
    {
      rejectClassStrategy = bowCV::BOW_REJECT_CLASS_AS_MULTICLASS;
      localAndClientMsg(VLogger::DEBUG, _callback,
                        "BOW will treat samples with Negative purpose as a separate class\n");
    }
    else if (0==strat.compare(bowCV::BOW_REJECT_CLASS_AS_FIRST_STAGE))
    {
      rejectClassStrategy = bowCV::BOW_REJECT_CLASS_AS_FIRST_STAGE;
      localAndClientMsg(VLogger::DEBUG, _callback,
                        "BOW will create a two-stage classifier: reject first, multiclass second\n");
      localAndClientMsg(VLogger::ERROR, _callback,
                        "BOW two-stage classifier is not implemented yet\n");
      return false;
    }
    else
    {
      localAndClientMsg(VLogger::WARN, _callback,
                        "Incorrect specifier for %s property, using default (%s).\n",
                        bowCV::BOW_REJECT_CLASS_STRATEGY.c_str(), rejectClassStrategy.c_str() );
    }      
  }
  
  bowCV* pBowCV = new bowCV(this);
  bool fInitialized =
    pBowCV->train_initialize(_nameFeature,_nameDescriptor,_nameMatcher,_countWords, &dda);
  if (fInitialized)
  {
    return pBowCV;
  }
  else
  {
    return NULL;
  }
}


void BowICETrainI::destroy(const Current& current)
{
}
std::string BowICETrainI::getName(const Current& current)
{
  if (mServiceMan) return mServiceMan->getServiceName();
  return "bowTrain";
}
std::string BowICETrainI::getDescription(const Current& current)
{
	return "BOW - Bag of Words trainer";
}

bool BowICETrainI::cancel(const Identity &client, const Current& current)
{
    stopping(); 
    mServiceMan->waitForStopService();
    if (mServiceMan->isStopCompleted())
        return true;
    else 
        return false;
 
}
cvac::TrainerProperties BowICETrainI::getTrainerProperties(const Current &current)
{
    //TODO get the real trainer properties but for now return an empty one.
    TrainerProperties tprops;
    return tprops;
}

void BowICETrainI::processSingleImg(string _filepath,string _filename,int _classID,
                                    const ::LocationPtr& _ploc,
                                    bowCV* pBowCV,
                                    TrainerCallbackHandlerPrx& _callback)
{
	std::string _strFilepath = std::string(_filepath);
	std::string _strFilename = std::string(_filename);	
	std::string _strFullname(_strFilepath + "/" + _strFilename);

	if(_ploc.get() == NULL)
        {
          pBowCV->train_stackTrainImage(_strFullname, _classID);
        }
	else
	{
          if(_ploc->ice_isA("::cvac::Silhouette"))
          {
            localAndClientMsg(VLogger::DEBUG, _callback, 
                              "Converting Silhouette in %s into BBox.\n",
                              _strFilename.c_str() );
            SilhouettePtr psil = SilhouettePtr::dynamicCast(_ploc);
            int xmax=-1, xmin=INT_MAX, ymax=-1, ymin=INT_MAX;
            for ( vector<Point2DPtr>::iterator ptit = psil->points.begin();
                  ptit!=psil->points.end(); ptit++)
            {
              Point2DPtr pt = *ptit;
              if (pt->x > xmax) xmax = pt->x;
              if (pt->x < xmin) xmin = pt->x;
              if (pt->y > ymax) ymax = pt->y;
              if (pt->y < ymin) ymin = pt->y;
            }
            pBowCV->train_stackTrainImage(_strFullname,_classID,
                                          xmin, ymin, xmax-xmin, ymax-ymin);
          }
          else if(_ploc->ice_isA("::cvac::BBox"))
          {
            BBoxPtr pbox = BBoxPtr::dynamicCast(_ploc);         
            pBowCV->train_stackTrainImage(_strFullname,_classID,
                                          pbox->x,pbox->y,pbox->width,pbox->height);
          }
          else
          {
            localAndClientMsg(VLogger::WARN, _callback,
                "Not adding %s because %d (not BBox or Silhouette) type.\n",
                _strFilename.c_str(), _classID);
            return;
          }
        }
        localAndClientMsg(VLogger::DEBUG, _callback, 
                          "Adding %s into training class %d.\n",
                          _strFilename.c_str(), _classID);
}


/** check for duplicate labels (with a different Purpose)
 */
bool hasUniqueLabels( const LabelMap& labelmap )
{
  set<string> lablist;
  for (LabelMap::const_iterator it=labelmap.begin(); it!=labelmap.end(); it++)
  {
    string name = it->second;
    if (lablist.find( it->second )!=lablist.end() )
    {
      return false;
    }
    lablist.insert( name );
  }
  return true;
}

/** argument error checking: any data? consistent multiclass or pos/neg purpose?
 *  having Negative purpose samples and multiclass is fine, but
 *  having Positive and multiclass is probably incorrect.  WARN.
 */
bool BowICETrainI::checkPurposedLists(
    const PurposedListSequence& purposedLists,
    TrainerCallbackHandlerPrx& _callback )
{
  if(purposedLists.size() == 0)
  {
    localAndClientMsg(VLogger::WARN, _callback, 
                      "Error: no data (runset) for processing\n");
    return false;
  }

  bool havemul = false;
  bool havepos = false;
  bool haveneg = false;
  maxClassId = -1;
  for (size_t listidx = 0; listidx < purposedLists.size(); listidx++)
  {
    Purpose& pur = purposedLists[listidx]->pur;
    switch(pur.ptype)
    {
    case cvac::POSITIVE:
      {
        havepos = true;
        break;
      }
    case cvac::NEGATIVE:
      {
        haveneg = true;
        break;
      }
    case cvac::MULTICLASS:
      {
        havemul = true;
        if (pur.classID>maxClassId)
        {
          maxClassId = pur.classID;
        }
        break;
      }
    }
  }
  if (havemul && havepos)
  {
    localAndClientMsg(VLogger::WARN, _callback,
                      "Your runset contains both Positive and Multiclass purposes. "
                      "This is unusual. Positives will be treated as a separate class.\n" );
  }
  localAndClientMsg(VLogger::DEBUG, _callback, "got %d purposed lists\n",
                    purposedLists.size());
  return true;
}

void BowICETrainI::process(const Identity &client,const ::RunSet& runset,
                           const TrainerProperties &tprops,
                           const Current& current)
{	
  localAndClientMsg(VLogger::DEBUG, NULL, "starting BOW training process\n");
  callbackPtr = TrainerCallbackHandlerPrx::uncheckedCast(
                     current.con->createProxy(client)->ice_oneway());		
  localAndClientMsg( VLogger::DEBUG_2, callbackPtr, 
                     "starting BOW training process, got callback pointer\n");

  PropertiesPtr props = (current.adapter->getCommunicator()->getProperties());
  std::string CVAC_DataDir = props->getProperty("CVAC.DataDir");

  // argument error checking: any data? consistent multiclass or pos/neg purpose?
  if (!checkPurposedLists( runset.purposedLists, callbackPtr ))
    return;

  DetectorDataArchive dda;
  bowCV* pBowCV = initialize(callbackPtr, tprops, dda, current);
  if ( NULL==pBowCV )
  {
    localAndClientMsg(VLogger::ERROR, callbackPtr,
                      "Trainer not initialized, aborting.\n");
    return;
  }

  // Ingest the data for processing, one purposed list at a time.
  // Also, determine if the classIDs match nicely to the labels;
  // if so, add appropriate annotations into the trained model file.
  LabelMap labelmap;
  bool labelsMatch = true;
  for (size_t listidx = 0; listidx < runset.purposedLists.size(); listidx++)
  {
    processPurposedList( runset.purposedLists[listidx], pBowCV,
                         callbackPtr, CVAC_DataDir,
                         labelmap, &labelsMatch );
  }
  if ( !labelsMatch ) labelmap.clear();
  if ( !hasUniqueLabels(labelmap) ) labelmap.clear();

  // create a sandbox for this client
  std::string connectName = cvac::getClientConnectionName(current);
  std::string clientName = mServiceMan->getSandbox()->createClientName(
    mServiceMan->getServiceName(), connectName);
  std::string tTempDir = mServiceMan->getSandbox()->createTrainingDir(clientName);
  // TODO: when should this tTempDir be deleted?

  localAndClientMsg(VLogger::INFO, callbackPtr, 
                    "Starting actual training procedure...\n"); 
  // Tell ServiceManager that we will listen for stop
  mServiceMan->setStoppable();

  //
  // run the actual training procedure on the previously ingested data;
  // this sets the pBowCV to point to files that it created,
  // and it adds some properties directly to the DDA
  //
  bool fTrain = pBowCV->train_run(tTempDir, mServiceMan);
  
  // Tell ServiceManager that we are done listening for stop
  mServiceMan->clearStop();  
  if(!fTrain)
  {
    deleteDirectory(tTempDir);
    localAndClientMsg(VLogger::ERROR, callbackPtr,
                      "Error during the training of BoW.\n");
    return;
  }

  // create the archive of the trained model
  FilePath trainedModel =
    createArchive( dda, pBowCV, labelmap, clientName, CVAC_DataDir, tTempDir );
  callbackPtr->createdDetector(trainedModel);

  delete pBowCV;
  pBowCV = NULL;

  localAndClientMsg(VLogger::INFO, callbackPtr, "Training procedure completed.\n");
}

int BowICETrainI::getPurposeId( const Purpose& pur,
                                TrainerCallbackHandlerPrx& _callback )
{
  switch (pur.ptype)
  {
  case cvac::POSITIVE:   return maxClassId+2;
  case cvac::NEGATIVE:   return maxClassId+1;
  case cvac::MULTICLASS: return pur.classID;
  default:
    {
      localAndClientMsg(VLogger::WARN, _callback,
                        "Unexpected Purpose: classID==%d, but not POS nor NEG\n",
                        pur.classID );
    }
  }
  return -1;
}

void BowICETrainI::processPurposedList( PurposedListPtr purList,
                                        bowCV* pBowCV,
                                        TrainerCallbackHandlerPrx& _callback,
                                        const string& CVAC_DataDir,
                                        LabelMap& labelmap, bool* pLabelsMatch
                                        )
{
  if (purList->pur.ptype==cvac::NEGATIVE
      && 0==rejectClassStrategy.compare(bowCV::BOW_REJECT_CLASS_IGNORE_SAMPLES))
  {
    // ignore Negative artifacts according to reject class strategy
    return;
  }
  int _classID = getPurposeId( purList->pur, _callback );
  PurposedLabelableSeq* lab = static_cast<PurposedLabelableSeq*>(purList.get());
  assert(NULL!=lab);
  
  if(lab->labeledArtifacts.size() == 0)
  {
    localAndClientMsg(VLogger::WARN, _callback,
                      "no actual labeledArtifacts in purposed list (%d)\n",
                      purList->pur.classID );
    // ignore and continue
  }
  
  for (size_t artfct=0; artfct< lab->labeledArtifacts.size(); artfct++)
  {						
    string fullName = getFSPath(lab->labeledArtifacts[artfct]->sub.path,
                                CVAC_DataDir);
    string _filename = getFileName(fullName);
    string _filepath = getFileDirectory(fullName);
    string labelname = lab->labeledArtifacts[artfct]->lab.name;
    
    LocationPtr pLoc = NULL;
    if(lab->labeledArtifacts[artfct]->ice_isA("::cvac::LabeledLocation"))
    {
      LabeledLocationPtr plabeledLocation =
        LabeledLocationPtr::dynamicCast(lab->labeledArtifacts[artfct]);
      if (plabeledLocation)
      {
        assert( NULL!=plabeledLocation.get() );
        pLoc = plabeledLocation->loc;				
      }
      else
      {
        printf("TODO: *** RunSetWrapper needs to fix this, it ignores the loc:\n");
        printf("WEIRD: *** why is ice_isA true but the cast returns null?\n");
      }
    }
    processSingleImg(_filepath,_filename, _classID, pLoc, pBowCV, _callback);

    // try to insert labelname into labelmap; abort if conflict
    Purpose pur = purList->pur;
    string prevname = labelmap[ purList->pur ];
    if (prevname.empty())
    {
      labelmap[ purList->pur ] = labelname;
    }
    else if ( prevname!=labelname )
    {
      *pLabelsMatch = false;
    }
  }
}

/** archive the trained model and return a CVAC style path to the archive;
 * This file should include the following:
 * Name of Detector
 * Name of Extractor
 * Name of Matcher
 * Filename of vocabulary
 * Filename of svm result
 * OpenCV Version
 * Optional: one-class ID
 */
FilePath BowICETrainI::createArchive( DetectorDataArchive& dda,
                                      bowCV* pBowCV,
                                      const LabelMap& labelmap,
                                      const string& clientName,
                                      const string& CVAC_DataDir,
                                      const string& tempDir )
{
  std::string clientDir = mServiceMan->getSandbox()->createClientDir(clientName);
  std::string archiveFilename = getDateFilename(clientDir,  "bow")+ ".zip";
 
  dda.setArchiveFilename(archiveFilename);
  dda.addFile(bowCV::BOW_VOC_FILE, tempDir + "/" + pBowCV->filenameVocabulary);
  dda.addFile(bowCV::BOW_SVM_FILE, tempDir + "/" + pBowCV->filenameSVM);
  for (LabelMap::const_iterator it=labelmap.begin(); it!=labelmap.end(); it++)
  {
    string classID = "labelname_" + getPurposeName( it->first );
    dda.setProperty( classID, it->second );
  }
  dda.createArchive(tempDir);
  mServiceMan->getSandbox()->deleteTrainingDir(clientName);

  // create a CVAC FilePath out of the DDA file system path
  FilePath file;
  file.filename = getFileName(archiveFilename);
  std::string relDir;
  int idx = clientDir.find(CVAC_DataDir.c_str(), 0, CVAC_DataDir.length());
  if (idx == 0)
  {
      relDir = clientDir.substr(CVAC_DataDir.length() + 1);
  }else
  {
      relDir = clientDir;
  }
  file.directory.relativePath = relDir;
  
  return file;
}

void BowICETrainI::message(MsgLogger::Levels msgLevel, const string& _msgStr)
{  
  localAndClientMsg((VLogger::Levels)msgLevel,callbackPtr,_msgStr.c_str());
}
