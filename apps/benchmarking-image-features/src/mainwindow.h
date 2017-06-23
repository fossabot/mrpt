#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#define NUM_DETECTORS 12
#define NUM_DESCRIPTORS 7
#define IMAGE_WIDTH 500
#define IMAGE_HEIGHT 500


#include <QMainWindow>
#include <QObject>
#include <QWidget>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QGroupBox>
#include <QListWidget>
#include <QComboBox>
#include <QFileDialog>
#include <QMap>
#include <QtGui>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "opencv2/core.hpp"
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/features2d.hpp"
#include "opencv2/xfeatures2d.hpp"


#include <mrpt/vision/CFeatureExtraction.h>

using namespace cv;
using namespace std;
using namespace mrpt::vision;
using namespace mrpt::utils;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=0);
    void ReadInputFormat();

public:



    QWidget *window_gui;
    QLabel *detector_label;

    QLabel *sample_image;
    QImage my_image;

    QPushButton *button_generate;
    QPushButton *button_close;

    QPushButton *prev_button;
    QPushButton *next_button;
    int current_imageIndex;

    QPushButton *browse_button;
    QPushButton *browse_button2;

    QGridLayout *layout_grid;


    int currentInputIndex;
    int detector_selected;
    int descriptor_selected;

    QGroupBox *groupBox1;
    QGroupBox *groupBox2;
    QGroupBox *groupBox_images;


    QComboBox *inputs;
    QLineEdit *inputFilePath;
    QLineEdit *inputFilePath2;
    string file_path1;
    string file_path2;

    QLineEdit *numFeaturesLineEdit;

    QComboBox *detectors_select;
    QComboBox *descriptors_select;

    QLabel *image1;
    QLabel *image2;
    QImage qimage1;
    QImage qimage2;

    QLabel *param1;
    QLabel *param2;
    QLabel *param3;
    QLabel *param4;
    QLabel *param5;

    QLineEdit *param1_edit;
    QLineEdit *param2_edit;
    QLineEdit *param3_edit;
    QLineEdit *param4_edit;
    QLineEdit *param5_edit;


    QLabel *param1_desc;
    QLabel *param2_desc;
    QLabel *param3_desc;
    QLabel *param4_desc;
    QLabel *param5_desc;

    QLineEdit *param1_edit_desc;
    QLineEdit *param2_edit_desc;
    QLineEdit *param3_edit_desc;
    QLineEdit *param4_edit_desc;
    QLineEdit *param5_edit_desc;

    //provide user options like stereo matching, activate/deactivate non-maximal suppression, image decimation, step-by-step playback of images.
    QCheckBox *stereo_matching;


    QLabel *output1;

    CFeatureExtraction fext;
    CFeatureList featsImage1, featsImage2;
    CImage img1, img2;
    TDescriptorType desc_to_compute;

    int numFeats;

    // Detector OPTIONS
    struct FASTOptions
    {
        float threshold;
        float min_distance;
        bool non_max_suppresion;
        bool use_KLT_response;
    }fast_opts;

    struct KLTOptions
    {
        float threshold;
        int radius;
        float min_distance;
        bool tile_image;
    }klt_opts;

    struct HarrisOptions
    {
        float threshold;
        float k;
        float sigma;
        float radius;
        float min_distance;
        bool tile_image;
    }harris_opts;

    struct SIFTOptions
    {
        float edge_threshold;
        float threshold;
    }SIFT_opts;

    struct SURFOptions
    {
        int hessianThreshold;
        int nLayersPerOctave;
        int nOctaves;
        bool rotation_invariant;
    }SURF_opts;

    struct ORBOptions
    {
        int min_distance;
        int n_levels;
        float scale_factor;
        bool extract_patch;
    }ORB_opts;

    //DESCRIPTOR OPTIONS
    struct SpinImageOptions
    {
        int radius;
        int hist_size_intensity;
        int hist_size_distance;
        float std_dist;
        float std_intensity;
    }spin_opts;
    struct PolarImageOptions
    {
        int radius;
        int bins_angle;
        int bins_distance;
    }polar_opts;
    struct LogPolarOptions
    {
        int radius;
        int num_angles;
        float rho_scale;

    }log_polar_opts;

signals:
    //void currentIndexChanged(int index);

public:
    void makeAllDetectorParamsVisible(bool flag);
    void makeAllDescriptorParamsVisible(bool flag);
    void fillDetectorInfo();
    void fillDescriptorInfo();
    void readFilesFromFolder(int next_prev);
    void displayImagesWithoutDetector();

public slots:
    void on_button_generate_clicked();
    void button_close_clicked();
    void on_detector_button_clicked();
    void on_descriptor_button_clicked();
    void on_browse_button_clicked();
    void on_browse_button_clicked2();
    void on_descriptor_choose(int choice);

    void on_detector_choose(int choice);
    void on_file_input_choose(int choice);

    void onStereoMatchingChecked(int state);
    void on_next_button_clicked();
    void on_prev_button_clicked();

};

#endif // MAINWINDOW_H
