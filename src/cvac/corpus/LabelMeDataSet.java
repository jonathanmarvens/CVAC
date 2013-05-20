package cvac.corpus;

import com.mathworks.toolbox.javabuilder.MWCellArray;
import com.mathworks.toolbox.javabuilder.MWArray;
import com.mathworks.toolbox.javabuilder.MWException;
import com.mathworks.toolbox.javabuilder.MWNumericArray;
import com.mathworks.toolbox.javabuilder.MWStructArray;
import cvac.CorpusCallback;
import cvac.DetectorPrx;
import cvac.DetectorPrxHelper;
import cvac.DirectoryPath;
import cvac.FilePath;
import cvac.Labelable;
import cvac.LabelablePrx;
import cvac.LabeledLocation;
import cvac.LabeledLocationPrx;
import cvac.LabeledLocationPrxHelper;
import cvac.Semantics;
import cvac.Silhouette;
import cvac.Substrate;
import util.Data_IO_Utils;
import util.DownloadUtils;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Properties;
import java.util.StringTokenizer;
import java.util.logging.Level;
import labelme.LabelMe;

/**
 * The connection to the LabelMe classes generated by MatlabJA
 * 
 * You need to have the Matlab Compiler Runtime (MCR) installed before this builds or runs.
 * 
 * On the target computer, append the following to your DYLD_LIBRARY_PATH environment variable:  
 * /Applications/MATLAB/MATLAB_Compiler_Runtime/v80/runtime/maci64:/Applications/MATLAB/MATLAB_Compiler_Runtime/v80/sys/os/maci64:/Applications/MATLAB/MATLAB_Compiler_Runtime/v80/bin/maci64:/System/Library/Frameworks/JavaVM.framework/JavaVM:/System/Library/Frameworks/JavaVM.framework/Libraries    
 * Next, set the XAPPLRESDIR environment variable to the following value:  
 * /Applications/MATLAB/MATLAB_Compiler_Runtime/v80/X11/app-defaults  

 * @author matz
 */
public class LabelMeDataSet extends CorpusI 
{
    private boolean isInitialized = false;
    private LabelMe lm;
    MWStructArray db = null;
    MWStructArray dbSpecific = null;
    String HOMEANNOTATIONS = "";
    String HOMEIMAGES = "";
    ArrayList<String> lmObjectLabelNames;
    ArrayList<String> lmFolderList;
    ArrayList<Labelable> ils;

    public LabelMeDataSet(String name, String description, String homepageURL, boolean isImmutableMirror)
    {
        super( name, description, homepageURL, isImmutableMirror );     
        this.lmObjectLabelNames = new ArrayList<String>(0);
        this.lmFolderList = new ArrayList<String>(0);
        this.ils = new ArrayList<Labelable>(0);
    }

    /**
     * Connects to LabelMe, downloads a database of objects from 
     * HOMEANNOTATIONS and all specified subfolders (via lmFolderList),
     * then restricts this database to a dbSpecific database based on the
     * lmObjectNames that the user is interested in.
     */
    private void init()
    {
        // make sure we have all the information we need:
        // image URL, annotation URL, object name(s) and folder name(s)
        if (lmObjectLabelNames.isEmpty() || lmFolderList.isEmpty() 
                || HOMEIMAGES.isEmpty() || HOMEANNOTATIONS.isEmpty())
        {
            throw new RuntimeException("incomplete LabelMe connection information");
        }
        
        logger.log( Level.INFO, "initializing connection to LabelMe");
        MWCellArray folderList = null;
        try {
            // clear out db if we're reconnecting
            if (null!=this.lm)
            {
                MWArray.disposeArray(db);
                MWArray.disposeArray(dbSpecific);
            } else
            {
                this.lm = new LabelMe();
            }
            
            folderList = new MWCellArray(lmFolderList.size(),1);
            for (int i=0; i<lmFolderList.size(); i++)
            {
                folderList.set(i+1, lmFolderList.get(i));
            }
            Object[] result1 = lm.LMdatabase( 2, HOMEANNOTATIONS, folderList );
            if (result1.length==0 || result1[0].getClass()==MWNumericArray.class)
            {
                logger.log( Level.INFO, "no images found on server - are you sure it's up?");
                return;
            }
            logger.log( Level.INFO, "successfully connected to LabelMe");
            db = (MWStructArray) result1[0];
            
            // only "OR" queries for object.name are currently allowed.  No
            // multiple "AND" queries with subsequent db intersection yet.
            logger.log( Level.INFO, "Querying LabelMe for object.name==\"{0}\"",
                    lmObjectLabelNames.get(0));
            Object[] result2 = lm.LMquery(1, db, "object.name", lmObjectLabelNames.get(0) );
            dbSpecific = (MWStructArray) result2[0];
            logger.log( Level.FINE, "obtained labeled subset from LabelMe");
            
            isInitialized = true;
        }
        catch (MWException ex) {
            logger.log( Level.WARNING, "error connecting to LabelMe");
            logger.log( Level.FINE, "exception is: ", ex);
        }
        finally {
            MWArray.disposeArray(folderList);
        }
    }
    
    public void close()
    {
        lm.dispose();
        lm = null;
        MWArray.disposeArray(db);
        MWArray.disposeArray(dbSpecific);
    }

    @Override
    public void addSample(String category, Labelable sam) {
        throw new RuntimeException("can't add samples to a LabelMeDataSet");
    }
    
    /**
     * download the images and their annotations from LabelMe to a local mirror
     */
    @Override
    public void loadImageAssets()
    {
        if (!isInitialized) 
        {
            init();
            if (!isInitialized) {
                logger.log(Level.WARNING, "can't initialize LabelMeDataSet, aborting loadImageAssets");
                return;
            }
        }
        
        // figure out what labels we have, how many of each, and create sample lists
        initSampleLists();
        
        try {
            // create local directory where to cache the images
            Data_IO_Utils.createDir_orFile( new File( m_dataSetFolder ), 
                    Data_IO_Utils.FS_ObjectType.DIRECTORY);
        } catch (IOException ex) {
            logger.log( Level.WARNING, "can't create directory {0}, will probably fail soon" );
        }
        
        // note the 1..n indexing instead of 0..n-1
        for (int labelmeObjnum = 1; labelmeObjnum<=dbSpecific.numberOfElements(); labelmeObjnum++)
        {
            String folder, filename, objname, sampleName;
            MWArray result = null, result0 = null, result1 = null, result2 = null, result3 = null;
            try 
            {
                // get the main image parameters
                result = dbSpecific.getField("annotation", labelmeObjnum);
                MWStructArray annot = (MWStructArray) result;
                result1 = annot.getField("folder", 1);
                folder = result1.toString();
                result2 = annot.getField("filename", 1);
                filename = result2.toString();
                

                // download the images from LabelMe
                String remoteURL = HOMEIMAGES + "/Images/" + folder + "/" + filename;
                logger.log( Level.FINE, "downloading image from {0}", remoteURL );
                String localFilename = m_dataSetFolder + "/" + folder + "/" + filename;
                File localFile = new File( localFilename );
                //debug
                System.out.println("file " + localFile);
                try {
                    // make sure the folder exists, then write the image
                    Data_IO_Utils.createDir_orFile( new File( m_dataSetFolder + "/" + folder ), 
                        Data_IO_Utils.FS_ObjectType.DIRECTORY);
                    DownloadUtils.URL_readToFile( remoteURL, localFile );
                }
                catch(IOException e) 
                {
                    logger.log(Level.WARNING, "cannot download from {0} or write to {1}",
                            new Object[]{ remoteURL, localFilename });
                    logger.log(Level.INFO, "", e);
                    continue;
                }
                DirectoryPath dirpath = new DirectoryPath( m_dataSetFolder + "/" + folder );
                FilePath path = new FilePath( dirpath, filename );
                int width = -1; int height = -1;
                Substrate im = new Substrate( true, false, path, width, height );
                
                // retrieve the number of annotated objects and the name
                // (annotation.object(m).name)
                
                result3 = annot.getField("object", 1);

                MWStructArray lmobj = (MWStructArray) result3;
                // TODO: maybe it's better to keep separate "ils", one for each labelmeObjnum?
                ils.ensureCapacity( ils.size() + lmobj.numberOfElements() );
                for (int objcnt=1; objcnt<=lmobj.numberOfElements(); objcnt++)
                {
                    MWArray result4 = lmobj.getField("name", objcnt);
                    objname = result4.toString();

                    Labelable annotation = getLabelable_Polygon(labelmeObjnum, objcnt, objname );
                    if (null!=annotation) {
                        logger.log(Level.FINEST, "polygon: {0}", annotation.toString() );
                        annotation.sub = im;
                        ils.add(annotation);
                    }
                }                
            }
            catch (Exception ex) {
                logger.log( Level.WARNING, "error getting folder/filename from sample annotation");
                logger.log( Level.INFO, "exception is: ", ex);
                continue;
            }
            finally {
                MWArray.disposeArray(result);
            }
            
        }
        logger.log(Level.INFO, "loaded {0} image assets", dbSpecific.numberOfElements());
    }
    
    @Override
    Labelable[] getLabels()
    {
        Labelable[] labels = ils.toArray( new Labelable[0] );
        return labels;
    }

    /**
     * Use LMobjectnames to obtain a list of all annotation objects and
     * their frequency in the dbSpecific database.
     */
    private void initSampleLists()
    {
        // Clear out old images
        m_images.clear();

        // list all annotations, create SampleLists/categories
        Object[] result = null;
        try 
        {
            result = lm.LMobjectnames(2, dbSpecific );
            if (result.length<2 || result[0].getClass()!=MWCellArray.class
                     || result[1].getClass()!=MWNumericArray.class)
            {
                throw new RuntimeException("unexpected return types");
            }
            MWCellArray names = (MWCellArray) result[0];
            MWNumericArray counts = (MWNumericArray) result[1];
            if (names.numberOfElements() != counts.numberOfElements())
            {
                throw new RuntimeException("unexpected return array sizes, "
                        + names.numberOfElements() + " vs " + counts.numberOfElements());
            }
            for (int cat=0; cat<names.numberOfElements(); cat++)
            {
                char[][] ca = (char[][]) names.get(cat+1);
                String name = String.copyValueOf( ca[0] );
                int count   = counts.getInt(cat+1);
                LabelableListI slist = new LabelableListI( name, this, name );
                m_images.put(name, slist);
                logger.log(Level.INFO, "found category {0} with {1} samples",
                        new Object[]{name, count});
            }
        }
        catch (Exception ex) {
            logger.log( Level.WARNING, "error listing all annotations");
            logger.log( Level.INFO, "exception is: ", ex);
        }
        finally {
            MWArray.disposeArray(result);
        }
    }        
    
    /**
     * TODO: This should return all polygons that correspond to objname,
     *  not just the first one
     * @param labelmeObjnum
     * @param objname
     * @return 
     */
    private Labelable getLabelable_Polygon( int labelmeObjnum, int objNum, String objname )
    {
        // get the polygon for the image annotation
        LabeledLocation polygonLoc = null;
        Object[] result = null;
        MWCellArray xs = null;
        MWCellArray ys = null;
        try {
            logger.log( Level.FINEST, "obtaining polygon from LabelMe");
            
            // get annotation polygon for LabelMe object number objnum, get maxnum polygons
 
//            result = lm.LMobjectpolygon(2, objname, maxnum);
            result = lm.LMobjectpolygon(2, dbSpecific.getField("annotation", labelmeObjnum), objNum);
            if (result.length!=2)
            {
                throw new RuntimeException("unexpected result from LMobjectpolygon");
            }
            logger.log( Level.FINEST, "got polygon from LabelMe");
            
            // convert to java-readable array
            xs = (MWCellArray) result[0];
            ys = (MWCellArray) result[1];
            float[][] points_x = (float[][]) xs.get(1);
            float[][] points_y = (float[][]) ys.get(1);
            int len = points_x.length;
            if (len!=points_y.length)
            {
                throw new RuntimeException("unequal number of x and y coordinates from LMobjectpolygon");
            }

            // create image annotation from polygon
            float[] nx = new float[len];
            float[] ny = new float[len];
            for (int cp=0; cp<len; cp++)
            {
                nx[cp] = points_x[cp][0];
                ny[cp] = points_y[cp][0];
            }

            // do length check again
            int len_nx = nx.length;
            if (ny.length!=len_nx)
            {
                throw new RuntimeException("need same number of x and y coords");
            }
            cvac.Point2D pts[] = new cvac.Point2D[len];
            int i;
            for (i = 0; i < len; i++){
                pts[i] = new cvac.Point2D((int)nx[i], (int)ny[i]);
            }
            Silhouette locSilhouette = new cvac.Silhouette(pts);
            
            polygonLoc = new LabeledLocation();
            polygonLoc.lab = new cvac.Label(true, objname, new HashMap<String,String>(0), new Semantics(""));
            polygonLoc.lab.hasLabel = true;                    // hasLabel
            polygonLoc.lab.name = "annotation";                // name
            polygonLoc.loc = locSilhouette;                    // location
        }
        catch (MWException ex) {
            logger.log( Level.WARNING, "error fetching polygons from LabelMe");
            logger.log( Level.FINE, "exception is: ", ex);
        }
        finally {
            MWArray.disposeArray(result);
        }
        
        return(polygonLoc);
    }

    @Override
    public void addCategory(LabelableListI samples) {
        throw new UnsupportedOperationException("Not supported in LabelMe Dataset.");
    }

    @Override
    public void removeSample(String category) {
        throw new UnsupportedOperationException("Not supported in LabelMe Dataset.");
    }

    /** The object.name to search for, as per LabelMe notation.
     * The query will search for all terms separated by comma.
     * 
     * @param text Can be, for example, car+side,building,road,tree
     */
    public void setObjectNames(String text) {
        if (null==text || text.equals(""))
        {
            throw new RuntimeException("object.name must not be null or the empty string");
        }
        // any actual changes?
        if (!lmObjectLabelNames.isEmpty() && text.equals(lmObjectLabelNames.get(0)))
            return;
        // only "OR" queries for object.name are currently allowed.  No
        // multiple "AND" queries with subsequent db intersection yet.
        lmObjectLabelNames.clear();
        lmObjectLabelNames.add(text);
        isInitialized = false;
    }

    /**
     * Folder list?
     * @param text Comma-separated list of folders to search for annotations in.
     */
    public void setLMFolders(String text) {
        if (null==text || text.equals(""))
        {
            throw new RuntimeException("LM folders must not be null or the empty string");
        }
        
        StringTokenizer t = new StringTokenizer( text, "," );
        ArrayList<String> fl = new ArrayList<String>();
        while (t.hasMoreTokens())
        {
            fl.add( t.nextToken() );
        }

        // any actual changes?
        if (fl.equals(lmFolderList))
            return;
        lmFolderList = fl;
        isInitialized = false;
    }

    public void setLMAnnotationURL( String url ) {
        // any actual changes?
        if (this.HOMEANNOTATIONS.equals(url))
            return;
        
        this.HOMEANNOTATIONS = url;
        isInitialized = false;
    }
    
    public void setLMImageURL( String url ) {
        // any actual changes?
        if (this.HOMEIMAGES.equals(url))
            return;

        this.HOMEIMAGES = url;
        isInitialized = false;
    }

    @Override
    public Properties getProperties() {
        Properties props = new Properties();
        props.setProperty("name", name);
        props.setProperty("description", description);
//        props.setProperty("imageType", imageType.toString());
        props.setProperty("datasetType", "labelme");
/*
        // object and folder names for LabelMe datasets
        props.setProperty("LMFolders", getLMFolders() );
        props.setProperty("LMObjectNames", getLMObjectName() );
        props.setProperty("LMAnnotationURL", HOMEANNOTATIONS);
        props.setProperty("LMImageURL", HOMEIMAGES);
*/        
        return props;
    }
    
    @Override
    public void configureFromProperties(Properties config) 
        throws CorpusConfigurationException
    {

        String folders = config.getProperty("LMFolders");
        if (null == folders){
            throw new CorpusConfigurationException("No LMFolders property");
        } else {
            this.setLMFolders(folders);
        }
        String objNames = config.getProperty("LMObjectNames");
        if (null == objNames){
            throw new CorpusConfigurationException("No LMObjectNames property");
        } else {
            this.setObjectNames(objNames);
        }
        this.HOMEANNOTATIONS = config.getProperty("LMAnnotationURL");
        if (null == this.HOMEANNOTATIONS){
            throw new CorpusConfigurationException("No LMAnnotationURL property");
        }
        this.HOMEIMAGES = config.getProperty("LMImageURL");
        if (null == this.HOMEIMAGES){
            throw new CorpusConfigurationException("No LMImageURL property");
        }
    }

//    public String getLMImageURL() {
//        return HOMEIMAGES;
//    }
//
//    public String getLMAnnotationURL() {
//        return HOMEANNOTATIONS;
//    }
//
//    public String getLMFolders() {
//        if (lmFolderList.isEmpty()) return "";
//        String ret = "";
//        for (String s : lmFolderList)
//        {
//            ret += s + ",";
//        }
//        return ret.substring(0, ret.length()-1);
//    }
//
//    /**
//     * String describing all object names to search for
//     * @return 
//     */
//    public String getLMObjectName() {
//        if (lmObjectLabelNames.isEmpty()) return "";
//        return lmObjectLabelNames.get(0);
//    }

    @Override
    void createLocalMirror(CorpusCallback cb)
    {
        // download everything for now, annotations and images
        loadImageAssets();
    }
}
