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
#include "BowICEI.h"
#include <iostream>
#include <vector>

#include <Ice/Communicator.h>
#include <Ice/Initialize.h>
#include <Ice/ObjectAdapter.h>
#include <util/processRunSet.h>
#include <util/FileUtils.h>
#include <util/DetectorDataArchive.h>
#include <util/ServiceManI.h>
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
    BowICEI *bow = new BowICEI();
    ServiceManagerI *sMan = new ServiceManagerI( bow, bow );
    bow->setServiceManager( sMan );
    return sMan;
  }
}


///////////////////////////////////////////////////////////////////////////////

BowICEI::BowICEI()
: pBowCV(NULL),fInitialized(false)
{
  callbackPtr = NULL;
    mServiceMan = NULL;
}

BowICEI::~BowICEI()
{
	delete pBowCV;
	pBowCV = NULL;
}

void BowICEI::setServiceManager(cvac::ServiceManagerI *sman)
{
    mServiceMan = sman;
}

void BowICEI::starting()
{
    // check if the config.service file contains a trained model
    // if so, read it.
    configModelFileName = mServiceMan->getModelFileFromConfig();
    if (configModelFileName.empty())
    {
        localAndClientMsg(VLogger::DEBUG, NULL, "No trained model file specified in service config.\n" );
    }
    else
    {
        localAndClientMsg(VLogger::DEBUG, NULL, "Will read trained model file as specified in service config: %s\n",
              configModelFileName.c_str()); 
    }
}

void BowICEI::stopping()
{
}


                          // Client verbosity
void BowICEI::initialize( DetectorDataArchive& dda,
                          const ::cvac::FilePath &file, const::Ice::Current &current)
{
  // Set CVAC verbosity according to ICE properties
  Ice::PropertiesPtr props = (current.adapter->getCommunicator()->getProperties());
  string verbStr = props->getProperty("CVAC.ServicesVerbosity");
  if (!verbStr.empty())
  {
    vLogger.setLocalVerbosityLevel( verbStr );
  }
  m_CVAC_DataDir = mServiceMan->getDataDir();

  // Since constructor only called on service start and destroy
  // can be called.  We need to make sure we have it
  if (pBowCV == NULL)
  {
    pBowCV = new bowCV(this);
  }
  cvac::FilePath model;
  if (configModelFileName.empty())
  {
      model = file;
  }else
  {
      model.directory.relativePath = getFileDirectory(configModelFileName);
      model.filename = getFileName(configModelFileName);
  }
	
  // Get the default CVAC data directory as defined in the config file
  std::string expandedSubfolder = "";
  std::string filename = "";
  std::string _extFile = model.filename.substr( model.filename.rfind(".")+1,
                                                    model.filename.length());
  std::string connectName = getClientConnectionName(current);
  std::string clientName = mServiceMan->getSandbox()->createClientName(mServiceMan->getServiceName(),
                                                             connectName);                               
  std::string clientDir = mServiceMan->getSandbox()->createClientDir(clientName);

  if (_extFile.compare("txt") == 0)
  {
    localAndClientMsg(VLogger::ERROR, NULL,
      "For maintaining consistency, this approach (using txt file as a detectorData) is prohibited.\n");
    return;
  }

  std::string zipfilename;
  // Only support absolute paths if they are from the config file
  if (configModelFileName.empty() == false)
  {
      if (pathAbsolute(configModelFileName) == false)
	  zipfilename = m_CVAC_DataDir + "/" + configModelFileName;
      else
	  zipfilename = configModelFileName;
  }else
  {
      zipfilename = getFSPath( model, m_CVAC_DataDir );
  }
  dda.unarchive(zipfilename, clientDir);

  // add the CVAC.DataDir root path and initialize from dda  
  fInitialized = pBowCV->detect_initialize( &dda );

  if (!fInitialized)
  {
    localAndClientMsg(VLogger::WARN, NULL, "Failed to run CV detect_initialize\n");
  }
}



bool BowICEI::isInitialized()
{
	return fInitialized;
}
 
void BowICEI::destroy(const ::Ice::Current& current)
{
	if(pBowCV != NULL)
		delete pBowCV;
	pBowCV = NULL;

	fInitialized = false;
}
std::string BowICEI::getName(const ::Ice::Current& current)
{
	return "bowTest";
}
std::string BowICEI::getDescription(const ::Ice::Current& current)
{
	return "Bag of Words-type detector";
}

bool BowICEI::cancel(const Ice::Identity &client, const ::Ice::Current& current)
{
    stopping();
    mServiceMan->waitForStopService();
    if (mServiceMan->isStopCompleted())
        return true;
    else 
        return false;
}

DetectorProperties BowICEI::getDetectorProperties(const ::Ice::Current& current)
{	
    //TODO get the real detector properties but for now return an empty one.
    DetectorProperties props;
	return props;
}

ResultSet BowICEI::processSingleImg(DetectorPtr detector,const char* fullfilename)
{	
	ResultSet _resSet;	
	int _bestClass;	

	// Detail the current file being processed (DEBUG_1)
	std::string _ffullname = std::string(fullfilename);
	localAndClientMsg(VLogger::DEBUG_1, NULL, "%s is processing.\n", _ffullname.c_str());
	BowICEI* _bowCV = static_cast<BowICEI*>(detector.get());
        float confidence = 0.5f; // TODO: obtain some confidence from BOW
        bool result = _bowCV->pBowCV->detect_run(fullfilename, _bestClass);

    if(true == result) {
        localAndClientMsg(VLogger::DEBUG_1, NULL, "Detection, %s as Class: %d\n",
                          _ffullname.c_str(), _bestClass);

        Result _tResult;
        _tResult.original = NULL;

        // The original field is for the original label and file name.  Results need
        // to be returned in foundLabels.  If the DetectorDataArchive contains properties
        // of the sort labelname_0 = 'somelabel', then a detection of classID 0 will be
        // reported as 'somelabel'
        LabelablePtr labelable = new Labelable();
        ostringstream ss;
        ss << _bestClass;
        string lname = ss.str();
        string val = _bowCV->pBowCV->dda->getProperty("labelname_"+lname);
        if ( val.empty() )
        {
          labelable->lab.name = lname;
        }
        else
        {
          labelable->lab.name = val;
        }
        labelable->confidence = confidence;
        labelable->lab.hasLabel = true;
        _tResult.foundLabels.push_back(labelable);
        _resSet.results.push_back(_tResult);
    }
	
	return _resSet;
}

void BowICEI::process(const Ice::Identity &client,
                      const ::RunSet& runset, const ::cvac::FilePath &trainedModelFile,  
                      const::cvac::DetectorProperties &props,
                      const ::Ice::Current& current)
{
  callbackPtr = 
    DetectorCallbackHandlerPrx::uncheckedCast(current.con->createProxy(client)->ice_oneway());

  // this must not go out of scope before processRunSet has completed:
  DetectorDataArchive dda;

  initialize( dda, trainedModelFile, current);
  if (!fInitialized || NULL==pBowCV || !pBowCV->isInitialized())
  {
    localAndClientMsg(VLogger::ERROR, callbackPtr, "BowICEI not initialized, aborting.\n");
  }
  DoDetectFunc func = BowICEI::processSingleImg;

  try {
    processRunSet(this, callbackPtr, func, runset, m_CVAC_DataDir, mServiceMan);
  }
  catch (exception e) {
    localAndClientMsg(VLogger::ERROR, callbackPtr, "BOW detector could not process given file-path.\n");
  }
}

void BowICEI::message(MsgLogger::Levels msgLevel, const string& _msgStr)
{  
  localAndClientMsg((VLogger::Levels)msgLevel,callbackPtr,_msgStr.c_str());
}

