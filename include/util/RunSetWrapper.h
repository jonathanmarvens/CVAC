#pragma once
/****
 *CVAC Software Disclaimer
 *
 *This software was developed at the Naval Postgraduate School, Monterey, CA,
 *by employees of the Federal Government in the course of their official duties.
 *Pursuant to title 17 Section 105 of the United States Code this software
 *is not subject to copyright protection and is in the public domain. It is 
 *an experimental system.  The Naval Postgraduate School assumes no
 *responsibility whatsoever for its use by other parties, and makes
 *no guarantees, expressed or implied, about its quality, reliability, 
 *or any other characteristic.
 *We would appreciate acknowledgement and a brief notification if the software
 *is used.
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above notice,
 *      this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above notice,
 *      this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the Naval Postgraduate School, nor the name of
 *      the U.S. Government, nor the names of its contributors may be used
 *      to endorse or promote products derived from this software without
 *      specific prior written permission.
 *
 *THIS SOFTWARE IS PROVIDED BY THE NAVAL POSTGRADUATE SCHOOL (NPS) AND CONTRIBUTORS
 *"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL NPS OR THE U.S. BE LIABLE FOR ANY
 *DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****/

#include <Data.h>
#include <Services.h>

#ifdef WIN32
#include <util/wdirent.h>
#else
#include <dirent.h>
#endif

#include <util/processRunSet.h>
#include <iostream>

namespace cvac
{
  using namespace std;

  typedef string rsMediaType;
  
  string getLowercase(const string& _str);

  class RunSetWrapper
  {		
  public:
    RunSetWrapper(const RunSet* _runset,string _mediaRootPath,
                  ServiceManager *_sman);
    ~RunSetWrapper();		

  private:
    bool   mFlagIntialize;
    string mMediaRootPath;
    const RunSet* mRunset;
    ServiceManager* mServiceMan;
    ResultSet mResultSet;		//candidate list of LabelablePtr		      
    vector<rsMediaType> mResultSetType;
    std::vector<std::string> mTypeMacro;
    std::vector<std::string> mTypeMacro_Image;  //for the func. getTypeMacro
    std::vector<std::string> mTypeMacro_Video;  //for the func. getTypeMacro
  
  private:	//Basic Utility	 
    std::string getTypeMacro(const std::string& _path);//image,video and etc
    rsMediaType getTypeMicro(const string _aPath);//in detail: bmp, png, and so on.
    rsMediaType getTypeMicro(const LabelablePtr _pla);
    string convertToAbsDirectory(const string& _directory);
    string convertToAbsDirectory(const string& _directory,
                                    const string& _prefix);      
    bool isAbsDirectory(const string& _directory);

  private:    //In Future: do with more sophisticated structures  
    bool isInRunset(const string& _rDir,const string& _fname,
                           const vector<rsMediaType>& _types,
                           rsMediaType& _resType);    
  private:	//main functions
    void addToList(const LabelablePtr _pla,const rsMediaType _type,
                   cvac::Purpose);
    bool makeBasicList();	
    bool makeBasicList_parse(const string& _absDir,bool _recursive,
                             const string& _relDir,
                             const vector<rsMediaType>& _types,
                             cvac::Purpose purpose);	
  public:     
    ResultSet& getResultSet();
    vector<rsMediaType>& getResultSetType();
    bool isInitialized(){ return mFlagIntialize; };
    string getRootDir(){  return mMediaRootPath;  };

  public:     //test functions
    void      showList();        
  };
}

