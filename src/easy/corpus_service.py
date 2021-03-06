#
# Python implementation of a CVAC CorpusServer
# matz, Nov 2013
#

import os, sys
import threading, string
import ConfigParser
import StringIO
import Ice, IcePy
import cvac
import labelme, vatic

class LabelableListI:
    def _init__(self, name):
        self.name = name
        self.subdir = None
        self.llist = []
        
    def addImageSample(self, relativePath, label, confidence, imageName):
        sample = cvac.Labelable()
        path = cvac.FilePath(relativePath, imageName)
        sample.sub = cvac.Substrate(True, False, path, -1. -1)
        sample.confidence = confidence
        sample.lab = label
        self.llist.append(sample)
        
        
    def addAllSamplesInDir(self, directory, label, confidence, relativePath, 
                           recursive):
        if recursive == False:
            for filename in os.listdir(directory):
                if os.path.isfile(os.path.join(directory, filename)) \
                  and (filename != ".meta"):
                    self.addImageSample(self, directory, label, confidence,
                                        filename)
        else:
            for folder, subdirs, files in os.walk(directory):
                for filename in files:
                    path, nameOnly = os.path.split(filename)
                    if nameOnly != ".meta":
                        self.addImageSample(self, path, label, confidence,
                                            nameOnly)
                                                
''' 
CorpusI is the basic class for Corpus DataSets.  This applies the same
 label to all images in the dataset
'''
class CorpusI(cvac.Corpus):
    def __init__(self, name, description, homepageURL, location, CVAC_DataDir):
        cvac.Corpus.__init__(self, name, description, homepageURL, True)
        self.CVAC_DataDir = CVAC_DataDir
        self.dataSetFolder = location
        
    def loadImagesFromDir(self, labelName, directory, recurse):
        dirSampleList = LabelableListI(labelName)
        label = cvac.Label(True, labelName, {}, cvac.Semantics(""))
        dirSampleList.addAllSamplesInDir(directory, label, 1.0, directory, 
                                         recurse)
        return dirSampleList
        
    def getLabels(self):
        # TODO load images from the directory and return
        return None
    
'''
LabelMeCorpusI is a LabelMe corpus.  This reads the
xml annotation files that are part of the dataset to create
individual Labels for each image if available.
'''
class LabelMeCorpusI(CorpusI): 
    def __init__(self, name, description, homepageURL, location, CVAC_DataDir):
        CorpusI.__init__(self, name, description, homepageURL, location,
                         CVAC_DataDir)   
        self.folderList = []
        
    def parseConfigProperties(self, configProps, propFile):
        prop = configProps.get('LMFolders')
        if prop == None:
            print('No LMFolders property in file ' + propFile)
            return False
        self.folderList = [x.strip() for x in prop.split(',')]
        prop = configProps.get('LMObjectNames')
        if prop == None:
            print('No LMObjectNames property in file ' + propFile)
            return False
        self.objectLabelName = prop
        prop = configProps.get('LMAnnotationURL')
        if prop == None:
            print('No LMAnnotationURL property in file ' + propFile)
            return False
        self.homeAnnotations = prop
        prop = configProps.get('LMImageURL')
        if prop == None:
            print('No LMImageURL property in file ' + propFile)
            return False
        self.homeImages = prop
        return True
    
    def getLabels(self):
        localDir = self.dataSetFolder
        labels = []
        for folder in self.folderList:
            labels += labelme.parseFolder(localDir, self.homeAnnotations,
                                          self.homeImages, folder, 
                                          self.CVAC_DataDir)
        return labels

'''
VaticCorpusI is a VATIC corpus.  This reads the
VATIC-style annotation files that are part of the dataset to create
individual Labels for each video.
'''
class VaticCorpusI(CorpusI): 
    def __init__(self, name, description, homepageURL, location, CVAC_DataDir):
        CorpusI.__init__(self, name, description, homepageURL, location,
                         CVAC_DataDir)   
        self.folders = []
        self.videoFileNames = []
        
    def parseConfigProperties(self, configProps, propFile):
        prop = configProps.get('VideoFileNames')
        if prop == None:
            print('No VideoFileNames property in file ' + propFile)
            return False
        self.videoFileNames = [x.strip() for x in prop.split(',')]
        prop = configProps.get('Folders')
        if prop == None:
            print('No Folders property in file ' + propFile)
            return False
        self.folders = [x.strip() for x in prop.split(',')]
        prop = configProps.get('ObjectNames')
        if prop == None:
            print('No ObjectNames property in file ' + propFile)
            return False
        self.objectLabelNames = [x.strip() for x in prop.split(',')]
        prop = configProps.get('AnnotationFile')
        if prop == None:
            print('No AnnotationFile property in file ' + propFile)
            return False
        self.annotationFile = prop
        return True

    def getLabels(self):
        '''invoke the VATIC parser on every annotation file
        that is part of this corpus'''
        localDir = self.dataSetFolder
        labels = []
        for vidfile, framefolder in zip(self.videoFileNames, self.folders):
            annotfile = string.replace( self.annotationFile,
                                        '$VideoFileName', vidfile)
            try:
                labels += vatic.parse(self.CVAC_DataDir, localDir,
                                      vidfile, framefolder, annotfile)
            except IOError as exc:
                print exc
        return labels

class CorpusServiceI(cvac.CorpusService, threading.Thread):   

    def __init__(self, communicator):
        threading.Thread.__init__(self)
        self._communicator = communicator
        self._destroy = False
        self._clients = []
        self._cond = threading.Condition()
        self.mListTestFile=[]    
        self.CVAC_DataDir = self._communicator.getProperties().\
                  getProperty( "CVAC.DataDir" )
        self.ConnectionName = "localhost"
        self.ServiceName = ""
        self.corpToImp = {}
        print("Service started: Python CorpusService.")

    def destroy(self):
        self._cond.acquire()

        print("Exiting Python CorpusService")
        self._destroy = True

        try:
            self._cond.notify()
        finally:
            self._cond.release()

        self.join()
        
    def addCorpusFromConfig(self, cvacPath):
        propFile = os.path.join(self.CVAC_DataDir, 
                                cvacPath.directory.relativePath,
                                cvacPath.filename)
        # since our config file does not have sections and we need one so we
        # create a string with the required header and file contents
        # and pass that to the parser
        with open(propFile, 'r') as f:     
            fileStr = '[main]\n' + f.read()
            str_fp = StringIO.StringIO(fileStr)
        config = ConfigParser.RawConfigParser()
        # Tell parser not to convert keys to lower case!
        config.optionxform = str
        config.readfp(str_fp)
        configProps = config._sections['main']
        corpus = self.parseCorpusProperties(configProps, propFile)
        if corpus == None:
            return None
        corp = self.corpToImp.get(corpus.name)
        if corp == None:
            self.corpToImp[corpus.name] = corpus
            return corpus
        else:
            return corpus
    
    def parseCorpusProperties(self, configProps, propFile):
        file_version = configProps['dataset_config_version']   
        version = float(file_version)
        cur_version = 1.1
        if version < cur_version:
            print('Config File is a prior version, need to update it for '
                  + str(cur_version))
            return None
        nameProp = configProps.get('name')
        if nameProp == None:
            print('No name property in file ' + propFile)
            return None
        descProp = configProps.get('description')
        if descProp == None:
            print('No description property in file ' + propFile)
            return None 
        homepage = configProps.get('homepage')
        if homepage == None:
            print('No homepage property in file ' + propFile)
        imgTypeProp = configProps.get('imageType')
        if imgTypeProp == None:
            print('No imageType property in file ' + propFile)
            return None
        locProp = configProps.get('main_location')
        if locProp == None:
            print('Python corpus currently only supports local dataset '
                  'with main_location')
            return None
        dsTypeProp = configProps.get('datasetType')
        if dsTypeProp.lower() == 'labelme':
            corpus = LabelMeCorpusI(nameProp, descProp, homepage, 
                                    locProp, self.CVAC_DataDir )
        elif dsTypeProp.lower() == 'vatic':
            corpus = VaticCorpusI(nameProp, descProp, homepage,
                                  locProp, self.CVAC_DataDir )
        else:
            print('Python corpus only currently supports labelme type dataset')
            return None
        if corpus.parseConfigProperties(configProps, propFile) == True:
            return corpus
            
         
        
    def openCorpus( self, cvacPath, current=None):
        if not type(cvacPath) is cvac.FilePath:
            raise RuntimeError("wrong argument type")
        print( 'openCorpus called' )
        return self.addCorpusFromConfig(cvacPath)
    
    def closeCorpus( self, corp, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        print( 'closeCorpus called' )
    
    def saveCorpus( self, corp, cvacPath, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        if not type(cvacPath) is cvac.FilePath:
            raise RuntimeError("wrong argument type")
        print( 'saveCorpus called' )
    
    def getDataSetRequiresLocalMirror( self, corp, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        print( 'getDataSetRequiresLocalMirror called' )
    
    def localMirrorExists( self, corp, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        print( 'localMirrorExists called' )
    
    def createLocalMirror( self, corp, callback, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        print( 'createLocalMirror called' )
    
    def getDataSet( self, corpus, current=None ):
        '''
        if pydevd' in sys.modules:
            import pydevd
            pydevd.connected = True
            pydevd.settrace(suspend=False)
        '''
        if not type(corpus) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        print( 'getDataSet called' )
        corp = self.corpToImp.get(corpus.name)
        if corp == None:
            return None
        labellist = corp.getLabels()
        return labellist
            
    
    def addLabelable( self, corp, addme, current=None ):
        if not type(corp) is cvac.Corpus:
            raise RuntimeError("wrong argument type")
        if not type(addme) is cvac.LabelableList:
            raise RuntimeError("wrong argument type")
        print( 'addLabelable called' )
    
    def createCorpus( self, cvacdir, current=None ):
        if not type(cvacdir) is cvac.DirectoryPath:
            raise RuntimeError("wrong argument type")
        print( 'createCorpus called' )


class Server(Ice.Application):
    def run(self, args):
        adapter = self.communicator().\
                     createObjectAdapter("PythonCorpusService")
        sender = CorpusServiceI(self.communicator())
        adapter.add(sender, self.communicator().\
                    stringToIdentity("PythonCorpusService"))
        adapter.activate()
        
        sender.start()
        try:
            #print self.communicator().getProperties()
            self.communicator().waitForShutdown()
        finally:
            sender.destroy()

        return 0

app = Server()
sys.exit(app.main(sys.argv, "config.service"))
