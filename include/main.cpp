// Copyright(c) 2016 - Costantino Grana, Federico Bolelli, Lorenzo Baraldi and Roberto Vezzani
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
// 
// *Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and / or other materials provided with the distribution.
// 
// * Neither the name of YACCLAB nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <opencv2/core/core.hpp> 
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <deque>          
#include <list>           
#include <queue>
#include <string>
#include <array> 
#include <algorithm>
#include <functional>

#include "performanceEvaluator.h"
#include "configurationReader.h"
#include "labelingAlgorithms.h"
#include "foldersManager.h"
#include "progressBar.h"
#include "memoryTester.h"

using namespace cv;
using namespace std;

#include <sys/types.h>
#include <sys/stat.h>

const char kPathSeparator =
#ifdef _WIN32
                            '\\';
#else
                            '/';
#endif

#ifdef __APPLE__
    const string terminal = "postscript";
    const string terminalExtension = ".ps"; 
#else
    const string terminal = "pdf";
    const string terminalExtension = ".pdf";
#endif

// Create a bunch of pseudo random colors from labels indexes and create a
// color representation for the labels
void colorLabels(const Mat1i& imgLabels, Mat3b& imgOut) {
	imgOut = Mat3b(imgLabels.size());
	for (int r = 0; r<imgLabels.rows; ++r) {
		for (int c = 0; c<imgLabels.cols; ++c) {
			imgOut(r, c) = Vec3b(imgLabels(r, c) * 131 % 255, imgLabels(r, c) * 241 % 255, imgLabels(r, c) * 251 % 255);
		}
	}
}

// This function may be useful to compare the output of different labeling procedures
// which may assign different labels to the same object. Use this to force a row major
// ordering of labels.
void normalizeLabels(Mat1i& imgLabels) {
	
	map<int,int> mapNewLabels;
	int iMaxNewLabel = 0;

	for (int r = 0; r<imgLabels.rows; ++r) {
		uint * const imgLabels_row = imgLabels.ptr<uint>(r); 
		for (int c = 0; c<imgLabels.cols; ++c) {
			int iCurLabel = imgLabels_row[c];
			if (iCurLabel>0) {
				if (mapNewLabels.find(iCurLabel) == mapNewLabels.end()) {
					mapNewLabels[iCurLabel] = ++iMaxNewLabel;
				}
				imgLabels_row[c] = mapNewLabels.at(iCurLabel);
			}
		}
	}
}

// Get binary image given a image's FileName; 
bool getBinaryImage(const string FileName, Mat1b& binaryMat){

	// Image load
	Mat image;
    image = imread(FileName, CV_LOAD_IMAGE_GRAYSCALE);   // Read the file

    // Check if image exist
	if (image.empty())
		return false;

	// Adjust the threshold to actually make it binary
	threshold(image, binaryMat, 100, 1, CV_THRESH_BINARY);

	return true;
}

// Compare two int matrixes element by element
bool compareMat(const Mat1i& mata, const Mat1i& matb){

    // Get a matrix with non-zero values at points where the 
    // two matrices have different values
    cv::Mat diff = mata != matb;
    // Equal if no elements disagree
    return cv::countNonZero(diff) == 0;
}

// This function is useful to delete eventual carriage return from a string 
// and is especially designed for windows file newline format
void deleteCarriageReturn(string &s){
	size_t found;
	do{
		// The while cycle is probably unnecessary
		found = s.find("\r");
		if (found != string::npos)
			s.erase(found, 1);
	} while (found != string::npos);

	return; 
}


// To check the correctness of algorithms on datasets specified
void checkAlgorithms(vector<pair<CCLPointer, string>>& CCLAlgorithms, const vector<string>& datasets, const string& input_path, const string& input_txt){

    vector<bool> stats(CCLAlgorithms.size(), true); // true if the i-th algorithm is correct, false otherwise
    vector<string> firstFail(CCLAlgorithms.size()); // name of the file on which algorithm fails the first time
    bool stop = false; // true if all algorithms are incorrect
    bool checkPerform = false; // true if almost one check was execute 

    for (uint i = 0; i < datasets.size(); ++i){
        // For every dataset in check list

        cout << "Test on " << datasets[i] << " starts: " << endl; 

        string is_path = input_path + kPathSeparator + datasets[i] + kPathSeparator + input_txt;
        
        ifstream supp(is_path);
        if (!supp.is_open()){
            cout<< "Unable to open " + is_path;
            continue; 
        }
        // Count number of lines to display progress bar
        int fileNumber = (int)std::count(std::istreambuf_iterator<char>(supp), std::istreambuf_iterator<char>(), '\n');
        supp.close();
        //Reopen file
        ifstream is(is_path);
        if (!is.is_open()){
            cout << "Unable to open " + is_path<< endl;
            continue;
        }
        
        // Count number of lines to display progress bar
        uint currentNumber = 0;

        string filename;
        while (getline(is, filename) && !stop){

			deleteCarriageReturn(filename); 

            // Display "progress bar"
            if (currentNumber * 100 / fileNumber != (currentNumber - 1) * 100 / fileNumber){
                cout << currentNumber << "/" << fileNumber << "         \r";
                fflush(stdout); 
            }
            currentNumber++;

            Mat1b binaryImg;
            Mat1i labeledImgCorrect, labeledImgToControl;
            unsigned nLabelsCorrect, nLabelsToControl;

            if (!getBinaryImage(input_path + kPathSeparator + datasets[i] + kPathSeparator + filename, binaryImg)){
                cout << "Unable to check on '" + filename + "', file does not exist" << endl;
                continue;
            }

			// SAUF_OPT������OpenCV����������еĿ�Դ�㷨��BBDTҲ��OpenCV�������еĿ�Դ�㷨���ڴ���Ϊ��׼�ο����ж��㷨�Ƿ���ȷ
			// ������������ͨ������Ƿ����׼SAUF_OPT�Ƿ�һ������ȷ������Ҫ�ģ���β����ٶȺ�Ч�ʣ��ڴ�����
            nLabelsCorrect = SAUF_OPT(binaryImg, labeledImgCorrect); // SAUF is the reference (the labels are already normalized)
            uint j = 0; 
            for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it, ++j){
                // For all the Algorithms in the array
                checkPerform = true; 
                if (stats[j]){
                    nLabelsToControl = (*it).first(binaryImg, labeledImgToControl);
                    normalizeLabels(labeledImgToControl);
                    if (nLabelsCorrect != nLabelsToControl || !compareMat(labeledImgCorrect, labeledImgToControl)){
                        stats[j] = false;
                        firstFail[j] = input_path + kPathSeparator + datasets[i] + kPathSeparator + filename; 
                        if (adjacent_find(stats.begin(), stats.end(), not_equal_to<int>()) == stats.end()){
                            stop = true; 
                            break;
                        }
                    }
                }
            }
        }// END WHILE (LIST OF IMAGES)
        cout << currentNumber << "/" << fileNumber << "\n" << "Test on " << datasets[i] << " ends " << endl;
        fflush(stdout);
    }// END FOR (LIST OF DATASETS)

    if (checkPerform){
        uint j = 0;
        for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it, ++j){
            if (stats[j])
                cout << "\"" << (*it).second << "\" is correct!" << endl;
            else
                cout << "\"" << (*it).second << "\" is not correct, it first fails on " << firstFail[j] << endl;
        }
    }
    else{
        cout << "Unable to perform check, skipped" << endl; 
    }
}

// This function take a char as input and return the corresponding int value (not ASCII one)
unsigned int ctoi(const char &c){
	return ((int)c - 48);
}

// This function help us to manage '\' escape character
void eraseDoubleEscape(string& str){
    for (uint i = 0; i < str.size() - 1; ++i){
        if (str[i] == str[i + 1] && str[i] == '\\')
            str.erase(i, 1);
    }
}

// This function take a Mat1d of results and save it on specified outputstream
void saveBroadOutputResults(const Mat1d& results, const string& oFileName, vector<pair<CCLPointer, string>>& CCLAlgorithms, const bool& write_n_labels,const Mat1i& labels, const vector<pair<string, bool>>& filesNames){
    
    ofstream os(oFileName); 
    if (!os.is_open()){
        cout << "Unable to save middle results" << endl; 
        return; 
    }

    // To set heading file format
    os << "#";
    for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it){
        os << "\t" << (*it).second;
        write_n_labels ? os << "\t" << "n_label" : os << "";
    }
    os << endl;
    // To set heading file format
    
    for (uint files = 0; files < filesNames.size(); ++files){
        if (filesNames[files].second){
            os << filesNames[files].first << "\t";
            unsigned int i = 0;
            for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it, ++i){
                os << results(files, i) << "\t";
                write_n_labels ? os << labels(files,i) << "\t" : os << "";
            }
            os << endl;
        }
    }
}

string averages_test(vector<pair<CCLPointer, string>>& CCLAlgorithms, Mat1d& all_res, const unsigned int& alg_pos, const string& input_path, const string& input_folder, const string& input_txt, const string& gnuplot_scipt_extension, string& output_path, string& colors_folder, const bool& saveMiddleResults, const uint& nTest, const string& middleFolder, const bool& write_n_labels = true, const bool& output_colors = true){

    string output_folder = input_folder,
		   complete_output_path = output_path + kPathSeparator + output_folder,
           gnuplot_script = input_folder + gnuplot_scipt_extension,
           output_broad_results = input_folder + "_results.txt",
           middleFile = input_folder + "_run",
           output_averages_results = input_folder + "_averages.txt",
		   output_graph = output_folder + terminalExtension,
           output_graph_bw = output_folder + "_bw" + terminalExtension,
           middleOut_Folder = complete_output_path + kPathSeparator + middleFolder,
           out_color_folder = output_path + kPathSeparator + output_folder + kPathSeparator + colors_folder;

    uint number_of_decimal_digit_to_display_in_graph = 2;

    // Creation of output path
	if (!dirExists(complete_output_path.c_str()))
		if (0 != std::system(("mkdir " + complete_output_path).c_str()))
			return ("Averages_Test on '" + input_folder + "': Unable to find/create the output path " + complete_output_path);

    if (output_colors){
        // Creation of color output path
        if (!dirExists(out_color_folder.c_str()))
            if (0 != std::system(("mkdir " + out_color_folder).c_str()))
                return ("Averages_Test on '" + input_folder + "': Unable to find/create the output path " + out_color_folder);
    }

    if (saveMiddleResults){
        if (!dirExists(middleOut_Folder.c_str()))
            if (0 != std::system(("mkdir " + middleOut_Folder).c_str()))
                return ("Averages_Test on '" + input_folder + "': Unable to find/create the output path " + middleOut_Folder);
    }

	string is_path = input_path + kPathSeparator + input_folder + kPathSeparator + input_txt,
		   os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_broad_results,
		   averages_os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_averages_results;
    
    // For AVERAGES RESULT
    ofstream averages_os(averages_os_path);
    if (!averages_os.is_open())
        return ("Averages_Test on '" + input_folder + "': Unable to open " + averages_os_path);
	// For LIST OF INPUT IMAGES
	ifstream is(is_path);
	if (!is.is_open())
		return ("Averages_Test on '" + input_folder + "': Unable to open " + is_path);
    
    // To save list of filename on which CLLAlgorithms must be tested
    vector<pair<string, bool>> filesNames;  // first: filename, second: state of filename (find or not)
    string filename;
    while (getline(is, filename)){
        // To delete eventual carriage return in the file name (especially designed for windows file newline format) 
        size_t found;
        do{
            // The while cycle is probably unnecessary
            found = filename.find("\r");
            if (found != string::npos)
                filename.erase(found, 1);
        } while(found != string::npos);
        // Add purified file name in the vector
        filesNames.push_back(make_pair(filename,true)); 
    }
    is.close();

    // Number of files
    int fileNumber = filesNames.size(); 

    // To save middle/min and averages results; 
    Mat1d min_res(fileNumber, CCLAlgorithms.size(), numeric_limits<double>::max());
    Mat1d current_res(fileNumber, CCLAlgorithms.size(), numeric_limits<double>::max());
    Mat1i labels(fileNumber, CCLAlgorithms.size(), 0);
    vector<pair<double, uint16_t>> supp_averages(CCLAlgorithms.size(), make_pair(0, 0));

    // Test is executed nTest times
    for (uint test = 0; test < nTest; ++test){

        // Count number of lines to display "progress bar"
        uint currentNumber = 0;

        PerformanceEvaluator perf;
        // For every file in list
        for (uint file = 0; file < filesNames.size(); ++file){
           
            string filename = filesNames[file].first;

            // Display "progress bar"
            if (currentNumber * 100 / fileNumber != (currentNumber - 1) * 100 / fileNumber){
                cout << "Test #" << (test + 1) << ": " << currentNumber << "/" << fileNumber << "         \r";
                fflush(stdout);
            }
            currentNumber++;

            Mat1b binaryImg;

            if (!getBinaryImage(input_path + kPathSeparator + input_folder + kPathSeparator + filename, binaryImg)){
                if (filesNames[file].second)
                    cout << "'" + filename + "' does not exist" << endl;
                filesNames[file].second = false;
                continue;
            }

            unsigned int i = 0;
            // For all the Algorithms in the array
            for (auto it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it, ++i){

                // This variables need to be redefined for every algorithms to uniform performance result (in particular this is true for labeledMat?)
                Mat1i labeledMat;
                unsigned nLabels;
                Mat3b imgColors;

                // Perform current algorithm on current image and save result
                perf.start((*it).second);
                nLabels = (*it).first(binaryImg, labeledMat);
                perf.stop((*it).second);

                // Save number of labels (we reasonably supposed that labels's number is the same on every #test so only the first time we save it)
                if (test == 0)
                    labels(file, i) = nLabels; 

                // Save time results 
                current_res(file, i) = perf.last((*it).second);
                if (perf.last((*it).second) < min_res(file, i))
                    min_res(file, i) = perf.last((*it).second);

                // If 'at_colorLabels' is enable only the fisrt time (test == 0) the output is saved
                if (test == 0 && output_colors){

                    // Remove gnuplot excape character from output filename
                    string algName = (*it).second;
                    algName.erase(std::remove(algName.begin(), algName.end(), '\\'), algName.end());

                    normalizeLabels(labeledMat);
                    colorLabels(labeledMat, imgColors);
                    imwrite(out_color_folder + kPathSeparator + filename + "_" + algName + ".png", imgColors);
                }

            }// END ALGORITHMS FOR
        } // END FILES FOR
        // To display "progress bar"
        cout << "Test #" << (test+1) << ": " << currentNumber << "/" << fileNumber << "         \r";
        fflush(stdout);

        // Save middle results if necessary (falg 'at_saveMiddleTests' enable) 
        if (saveMiddleResults){ 
            string middleOut = middleOut_Folder + kPathSeparator + middleFile + "_" + to_string(test) + ".txt";         
            saveBroadOutputResults(current_res, middleOut, CCLAlgorithms, write_n_labels, labels, filesNames);
        }
    }// END TESTS FOR

    // To wirte in a file min results
    saveBroadOutputResults(min_res, os_path, CCLAlgorithms, write_n_labels, labels, filesNames);
    
    // To calculate averages times and write it on the specified file
    for (int r = 0; r < min_res.rows; ++r){
        for (int c = 0; c < min_res.cols; ++c){
            if (min_res(r, c) != numeric_limits<double>::max()){
                supp_averages[c].first += min_res(r, c);
                supp_averages[c].second++; 
            }
        } 
    }

    averages_os << "#Algorithm" << "\t" << "Average" << "\t" << "Round Average for Graphs" << endl;
    for (unsigned int i = 0; i < CCLAlgorithms.size(); ++i){
        // For all the Algorithms in the array
        all_res(alg_pos, i) = supp_averages[i].first / supp_averages[i].second;
        averages_os << CCLAlgorithms[i].second << "\t" << supp_averages[i].first / supp_averages[i].second << "\t";
        averages_os << std::fixed << std::setprecision(number_of_decimal_digit_to_display_in_graph) << supp_averages[i].first / supp_averages[i].second << endl;
    }

	// GNUPLOT SCRIPT
	string scriptos_path = output_path + kPathSeparator + output_folder + kPathSeparator + gnuplot_script;
	ofstream scriptos(scriptos_path);
	if (!scriptos.is_open())
		return ("Averages_Test on '" + input_folder + "': Unable to create " + scriptos_path);

    scriptos << "# This is a gnuplot (http://www.gnuplot.info/) script!" << endl; 
    scriptos << "# comment fifth line, open gnuplot's teminal, move to script's path and launch 'load " << gnuplot_script << "' if you want to run it" << endl << endl;
    
    scriptos << "reset" << endl;   
	scriptos << "cd '" << complete_output_path << "\'" << endl;
    scriptos << "set grid ytic" << endl; 
    scriptos << "set grid" << endl << endl; 

    scriptos << "# " << output_folder << "(COLORS)" << endl;
    scriptos << "set output \"" + output_graph + "\"" << endl;
    scriptos << "#set title \"" + output_folder + "\" font ', 12'" << endl << endl;
    
    scriptos << "# " << terminal << " colors" << endl;
    scriptos << "set terminal "<< terminal <<" enhanced color font ',15'" << endl << endl;

    scriptos << "# Graph style"<< endl;
    scriptos << "set style data histogram" << endl;
	scriptos << "set style histogram cluster gap 1" << endl;
	scriptos << "set style fill solid 0.25 border -1" << endl;
	scriptos << "set boxwidth 0.9" << endl << endl;
	
    scriptos << "# Get stats to set labels" << endl;
    scriptos << "stats \"" << output_averages_results << "\" using 2 nooutput" << endl;
    scriptos << "ymax = STATS_max + (STATS_max/100)*10" << endl;
    scriptos << "xw = 0" << endl;
    scriptos << "yw = (ymax)/22" << endl << endl;

    scriptos << "# Axes labels" << endl;
    scriptos << "set xtic rotate by -45 scale 0" << endl;
    scriptos << "set ylabel \"Execution Time [ms]\"" << endl << endl;

    scriptos << "# Axes range" << endl; 
    scriptos << "set yrange[0:ymax]" << endl;
    scriptos << "set xrange[*:*]" << endl << endl;

    scriptos << "# Legend" << endl; 
    scriptos << "set key off" << endl << endl;

    scriptos << "# Plot" << endl; 
	scriptos << "plot \\" << endl; 
    scriptos << "'" + output_averages_results + "' using 2:xtic(1), '" << output_averages_results << "' using ($0 - xw) : ($2 + yw) : (stringcolumn(3)) with labels" << endl << endl;
	
    scriptos << "# " << output_folder << "(BLACK AND WHITE)" << endl;
    scriptos << "set output \"" + output_graph_bw + "\"" << endl;
    scriptos << "#set title \"" + output_folder + "\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal <<" black and white" << endl;
    scriptos << "set terminal " << terminal << " enhanced monochrome dashed font ',15'" << endl << endl;

    scriptos << "replot" << endl << endl;

    scriptos << "exit gnuplot" << endl;
	
	averages_os.close();
	scriptos.close();
    // GNUPLOT SCRIPT

    if (0 != std::system(("gnuplot " + complete_output_path + kPathSeparator + gnuplot_script).c_str()))
        return ("Averages_Test on '" + input_folder + "': Unable to run gnuplot's script");

	return ("Averages_Test on '" + input_folder + "': successfully done");
}

string density_size_test(vector<pair<CCLPointer, string>>& CCLAlgorithms, const string& input_path, const string& input_folder, const string& input_txt, const string& gnuplot_script_extension, string& output_path, string& colors_folder, const bool& saveMiddleResults, const uint& nTest, const string& middleFolder, const bool& write_n_labels = true, const bool& output_colors = true){
	
	string output_folder = input_folder,
		   complete_output_path = output_path + kPathSeparator + output_folder,
           gnuplot_script = input_folder + gnuplot_script_extension,
		   output_broad_result = input_folder + "_results.txt",
		   output_size_result = "size.txt",
           //output_size_normalized_result = "",
		   output_density_result = "density.txt",
           output_density_normalized_result = "normalized_density.txt",
		   output_size_graph = "size" + terminalExtension,
           output_size_graph_bw = "size_bw" + terminalExtension,
           output_density_graph = "density" + terminalExtension,
           output_density_graph_bw = "density_bw" + terminalExtension,
           output_normalization_density_graph = "normalized_density" + terminalExtension,
           output_normalization_density_graph_bw = "normalized_density_bw" + terminalExtension,
           middleFile = input_folder + "_run",
           middleOut_Folder = complete_output_path + kPathSeparator + middleFolder,
           out_color_folder = output_path + kPathSeparator + output_folder + kPathSeparator + colors_folder,
           output_NULL = input_folder + "_NULL_results.txt";

    // Creation of output path
	if (!dirExists(complete_output_path.c_str()))
		if (0 != std::system(("mkdir " + complete_output_path).c_str()))
			return ("Density_Size_Test on '" + input_folder + "': Unable to find/create the output path " + complete_output_path); 

    if (output_colors){
        // Creation of color output path
        if (!dirExists(out_color_folder.c_str()))
            if (0 != std::system(("mkdir " + out_color_folder).c_str()))
                return ("Density_Size_Test on '" + input_folder + "': Unable to find/create the output path " + out_color_folder);
    }

    if (saveMiddleResults){
        if (!dirExists(middleOut_Folder.c_str()))
            if (0 != std::system(("mkdir " + middleOut_Folder).c_str()))
                return ("Density_Size_Test on '" + input_folder + "': Unable to find/create the output path " + middleOut_Folder);
    }

	string is_path = input_path + kPathSeparator + input_folder + kPathSeparator + input_txt,
		   os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_broad_result,
		   density_os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_density_result,
           density_normalized_os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_density_normalized_result,
	       size_os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_size_result,
           //size_normalized_os_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_size_normalized_result, 
           NULL_path = output_path + kPathSeparator + output_folder + kPathSeparator + output_NULL;

    // For DENSITY RESULT
    ofstream density_os(density_os_path);
    if (!density_os.is_open())
        return ("Density_Size_Test on '" + input_folder + "': Unable to create " + density_os_path);
    // For DENSITY NORMALIZED RESULT
    ofstream density_normalized_os(density_normalized_os_path);
    if (!density_normalized_os.is_open())
        return ("Density_Size_Test on '" + input_folder + "': Unable to create " + density_normalized_os_path);
    // For SIZE RESULT
    ofstream size_os(size_os_path);
    if (!size_os.is_open())
        return ("Density_Size_Test on '" + input_folder + "': Unable to create " + size_os_path);
    // For LIST OF INPUT IMAGES
    ifstream is(is_path);
    if (!is.is_open())
        return ("Density_Size_Test on '" + input_folder + "': Unable to open " + is_path);
    // For NULL LABELING RESULTS
    ofstream NULL_os(NULL_path);
    if (!NULL_os.is_open())
        return ("Density_Size_Test on '" + input_folder + "': Unable to create " + NULL_path);

    // To save list of filename on which CLLAlgorithms must be tested
    vector<pair<string, bool>> filesNames;  // first: filename, second: state of filename (find or not)
    string filename;
    while (getline(is, filename)){
        // To delete eventual carriage return in the file name (especially designed for windows file newline format) 
        size_t found;
        do{
            // The while cycle is probably unnecessary
            found = filename.find("\r");
            if (found != string::npos)
                filename.erase(found, 1);
        } while (found != string::npos);
        filesNames.push_back(make_pair(filename, true));
    }
    is.close();

    // Number of files
    int fileNumber = filesNames.size();

    // To save middle/min and averages results;
    Mat1d min_res(fileNumber, CCLAlgorithms.size(), numeric_limits<double>::max());
    Mat1d current_res(fileNumber, CCLAlgorithms.size(), numeric_limits<double>::max());
    Mat1i labels(fileNumber, CCLAlgorithms.size(), 0);
    vector<pair<double, uint16_t>> supp_averages(CCLAlgorithms.size(), make_pair(0, 0));

    // To save labeling NULL results
    vector<double> NULL_labeling(fileNumber, numeric_limits<double>::max());

    // To set heading file format (SIZE RESULT, DENSITY RESULT)
    //os << "#";
    density_os << "#Density";
    size_os << "#Size";
    density_normalized_os << "#DensityNorm"; 
    for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it){
        //os << "\t" << (*it).second;
        //write_n_labels ? os << "\t" << "n_label" : os << "";
        density_os << "\t" << (*it).second;
        size_os << "\t" << (*it).second;
        density_normalized_os << "\t" << (*it).second;
    }
    //os << endl;
    density_os << endl;
    size_os << endl;
    density_normalized_os << endl;
    // To set heading file format (SIZE RESULT, DENSITY RESULT)
    
    uint8_t density = 9 /*[0.1,0.9]*/, size = 8 /*[32,64,128,256,512,1024,2048,4096]*/;

    vector<vector<pair<double, uint16_t>>> supp_density(CCLAlgorithms.size(), vector<pair<double, uint16_t>>(density, make_pair(0, 0)));
    vector<vector<pair<double, uint16_t>>> supp_normalized_density(CCLAlgorithms.size(), vector<pair<double, uint16_t>>(density, make_pair(0, 0)));
    vector<vector<pair<double, uint16_t>>> supp_size(CCLAlgorithms.size(), vector<pair<double, uint16_t>>(size, make_pair(0, 0)));
    //vector<vector<pair<double, uint16_t>>> supp_normalized_size(CCLAlgorithms.size(), vector<pair<double, uint16_t>>(size, make_pair(0, 0)));

    // Note that number of random_images is less than 800, this is why the second element of the 
    // pair has uint16_t data type. Extern vector represent the algorithms, inner vector represent 
    // density for "supp_density" variable and dimension for "supp_dimension" one. In particular: 
    //	
    //	FOR "supp_density" VARIABLE:	
    //	INNER_VECTOR[0] = { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_0.1_DENSITY, COUNT_OF_THAT_IMAGES }
    //	INNER_VECTPR[1] = { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_0.2_DENSITY, COUNT_OF_THAT_IMAGES }
    //  .. and so on;
    //
    //	SO: 
    //	  supp_density[0][0] represent the pair { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_0.1_DENSITY, COUNT_OF_THAT_IMAGES }
    //	  for algorithm in position 0;
    //	  
    //	  supp_density[0][1] represent the pair { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_0.2_DENSITY, COUNT_OF_THAT_IMAGES }
    //	  for algorithm in position 0;
    //	
    //	  supp_density[1][0] represent the pair { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_0.1_DENSITY, COUNT_OF_THAT_IMAGES }
    //	  for algorithm in position 1;
    //    .. and so on
    //
    //	FOR "SUP_DIMENSION VARIABLE": 
    //	INNER_VECTOR[0] = { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_32*32_DIMENSION, COUNT_OF_THAT_IMAGES }
    //	INNER_VECTOR[1] = { SUM_OF_TIME_FOR_CCL_OF_IMAGES_WITH_64*64_DIMENSION, COUNT_OF_THAT_IMAGES }
    //
    //	view "supp_density" explanation for more details;
    //

    // Test is execute nTest times
    for (uint test = 0; test < nTest; ++test){

        // Count number of lines to display "progress bar"
        uint currentNumber = 0;

        PerformanceEvaluator perf;
        // For every file in list
        for (uint file = 0; file < filesNames.size(); ++file){
            string filename = filesNames[file].first;

            // Display "progress bar"
            if (currentNumber * 100 / fileNumber != (currentNumber - 1) * 100 / fileNumber){
                cout << "Test #" << (test + 1) << ": " << currentNumber << "/" << fileNumber << "         \r";
                fflush(stdout);
            }
            currentNumber++;

            Mat1b binaryImg;
            Mat1i null_labels; 

            if (!getBinaryImage(input_path + kPathSeparator + input_folder + kPathSeparator + filename, binaryImg)){
                if (filesNames[file].second)
                    cout << "'" + filename + "' does not exist" << endl;
                filesNames[file].second = false;
                continue;
            }

            // One time for every test and for every image we execute the NULL labeling and get the minimum 
            perf.start("NULL_reference");
            labelingNULL(binaryImg, null_labels);
            perf.stop("NULL_reference");

            if (perf.last("NULL_reference") < NULL_labeling[file]){
                NULL_labeling[file] = perf.last("NULL_reference"); 
            }
            // One time for every test and for every image we execute the NULL labeling and get the minimum 

            unsigned int i = 0;
            for (vector<pair<CCLPointer, string>>::iterator it = CCLAlgorithms.begin(); it != CCLAlgorithms.end(); ++it, ++i){
                // For all the Algorithms in the array

                // This variable need to be redefined for every algorithms to uniform performance result (in particular this is true for labeledMat?)
                Mat1i labeledMat;
                unsigned nLabels;
                Mat3b imgColors;

                // Note that "i" represent the current algorithm's position in vectors "supp_density" and "supp_dimension"
                perf.start((*it).second);
                nLabels = (*it).first(binaryImg, labeledMat);
                perf.stop((*it).second);

                if (test == 0)
                    labels(file, i) = nLabels;

                // Save time results 
                current_res(file, i) = perf.last((*it).second);
                if (perf.last((*it).second) < min_res(file, i))
                    min_res(file, i) = perf.last((*it).second);

                // If 'at_colorLabels' is enable only the fisrt time (test == 0) the output is saved
                if (test == 0 && output_colors){
                    // Remove gnuplot excape character from output filename
                    string algName = (*it).second;
                    algName.erase(std::remove(algName.begin(), algName.end(), '\\'), algName.end());

                    normalizeLabels(labeledMat);
                    colorLabels(labeledMat, imgColors);
                    imwrite(out_color_folder + kPathSeparator + filename + "_" + algName + ".png", imgColors);
                }
            }// END ALGORTIHMS FOR
        } // END FILES FOR 
        // To display "progress bar"
        cout << "Test #" << (test + 1) << ": " << currentNumber << "/" << fileNumber << "         \r";
        fflush(stdout);

        // Save middle results if necessary (falg 'at_saveMiddleTests' enable) 
        if (saveMiddleResults){
            string middleOut = middleOut_Folder + kPathSeparator + middleFile + "_" + to_string(test) + ".txt";
            saveBroadOutputResults(current_res, middleOut, CCLAlgorithms, write_n_labels, labels, filesNames);
        }
	}// END TEST FOR

    // To wirte in a file min results
    saveBroadOutputResults(min_res, os_path, CCLAlgorithms, write_n_labels, labels, filesNames);
    
    // To sum min results, in the correct manner, before make averages
    for (unsigned int files = 0; files < filesNames.size(); ++files){
        // Note that files correspond to min_res rows
        for (int c = 0; c < min_res.cols; ++c){
            // Add current time to "supp_density" and "supp_size" in the correct position 
            if (isdigit(filesNames[files].first[0]) && isdigit(filesNames[files].first[1]) && isdigit(filesNames[files].first[2]) && filesNames[files].second){
                if (min_res(files,c) != numeric_limits<double>::max()){ // superfluous test?
                    // For density graph
                    supp_density[c][ctoi(filesNames[files].first[1])].first += min_res(files, c);
                    supp_density[c][ctoi(filesNames[files].first[1])].second++;

                    // For normalized desnity graph
                    supp_normalized_density[c][ctoi(filesNames[files].first[1])].first += (min_res(files, c)) / (NULL_labeling[files]);
                    supp_normalized_density[c][ctoi(filesNames[files].first[1])].second++;

                    // For dimension graph
                    supp_size[c][ctoi(filesNames[files].first[0])].first += min_res(files, c);
                    supp_size[c][ctoi(filesNames[files].first[0])].second++;
                }
            }
            // Add current time to "supp_density" and "supp_size" in the correct position
        }
    }
    // To sum min results, in the correct manner, before make averages


	// To calculate averages times
	vector<vector<long double>> density_averages(CCLAlgorithms.size(), vector<long double>(density)), size_averages(CCLAlgorithms.size(), vector<long double>(size));
    vector<vector<long double>> density_normalized_averages(CCLAlgorithms.size(), vector<long double>(density));
    for (unsigned int i = 0; i < CCLAlgorithms.size(); ++i){
		// For all algorithms
		for (unsigned int j = 0; j < density_averages[i].size(); ++j){
			// For all density and normalized density
            if (supp_density[i][j].second != 0){
                density_averages[i][j] = supp_density[i][j].first / supp_density[i][j].second;
                density_normalized_averages[i][j] = supp_normalized_density[i][j].first / supp_normalized_density[i][j].second;
            }
            else{
                // If there is no element with this density characyteristic the averages value is set to zero
                density_averages[i][j] = 0.0;  
                density_normalized_averages[i][j] = 0.0;
            }
		}
		for (unsigned int j = 0; j < size_averages[i].size(); ++j){
			// For all size
			if (supp_size[i][j].second != 0)
				size_averages[i][j] = supp_size[i][j].first / supp_size[i][j].second;
			else
				size_averages[i][j] = 0.0;  // If there is no element with this size characyteristic the averages value is set to zero
		}
	}
	// To calculate averages

	// To write density result on specified file
	for (unsigned int i = 0; i < density; ++i){
		// For every density
        if (density_averages[0][i] == 0.0){ // Check it only for the first algorithm (it is the same for the others)
            density_os << "#"; // It means that there is no element with this density characyteristic 
            density_normalized_os << "#"; // It means that there is no element with this density characyteristic
        }
        density_os << ((float)(i + 1) / 10) << "\t"; //Density value
        density_normalized_os << ((float)(i + 1) / 10) << "\t"; //Density value
		for (unsigned int j = 0; j < density_averages.size(); ++j){
			// For every alghorithm
			density_os << density_averages[j][i] << "\t";
            density_normalized_os << density_normalized_averages[j][i] << "\t";
		}
		density_os << endl; // End of current line (current density)
        density_normalized_os << endl; // End of current line (current density)
	}
	// To write density result on specified file

	// To set sizes's label 
	vector <pair<unsigned int, double>> supp_size_labels(size, make_pair(0, 0));

	// To write size result on specified file
	for (unsigned int i = 0; i < size; ++i){
		// For every size
		if (size_averages[0][i] == 0.0) // Check it only for the first algorithm (it is the same for the others)
			size_os << "#"; // It means that there is no element with this size characyteristic 
		supp_size_labels[i].first = (int)(pow(2, i + 5));
		supp_size_labels[i].second = size_averages[0][i];
		size_os << (int)pow(supp_size_labels[i].first,2) << "\t"; //Size value
		for (unsigned int j = 0; j < size_averages.size(); ++j){
			// For every alghorithms
			size_os << size_averages[j][i] << "\t";
		}
		size_os << endl; // End of current line (current size)
	}
	// To write size result on specified file

    // To write NULL result on specified file
    for (unsigned int i = 0; i < filesNames.size(); ++i){
        NULL_os << filesNames[i].first << "\t" << NULL_labeling[i] << endl; 
    }
    // To write NULL result on specified file

	// GNUPLOT SCRIPT
	string scriptos_path = output_path + kPathSeparator + output_folder + kPathSeparator + gnuplot_script;
	ofstream scriptos(scriptos_path);
	if (!scriptos.is_open())
		return ("Density_Size_Test on '" + input_folder + "': Unable to create " + scriptos_path);

    scriptos << "# This is a gnuplot (http://www.gnuplot.info/) script!" << endl;
    scriptos << "# comment fifth line, open gnuplot's teminal, move to script's path and launch 'load " << gnuplot_script << "' if you want to run it" << endl << endl;
	
    scriptos << "reset" << endl; 
    scriptos << "cd '" << complete_output_path << "\'" << endl;
    scriptos << "set grid" << endl << endl; 

    // DENSITY
    scriptos << "# DENSITY GRAPH (COLORS)" << endl << endl; 

    scriptos << "set output \"" + output_density_graph + "\"" << endl;
    scriptos << "#set title \"Density\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal << " colors" << endl; 
    scriptos << "set terminal " << terminal << " enhanced color font ',15'" << endl << endl;

    scriptos << "# Axes labels" << endl; 
    scriptos << "set xlabel \"Density\"" << endl;
    scriptos << "set ylabel \"Execution Time [ms]\"" << endl << endl;

    scriptos << "# Axes range" << endl;
    scriptos << "set xrange [0:1]" << endl;
    scriptos << "set yrange [*:*]" << endl;
    scriptos << "set logscale y" << endl << endl;

    scriptos << "# Legend" << endl;
    scriptos << "set key left top nobox spacing 2 font ', 8'" << endl << endl;

    scriptos << "# Plot" << endl;
	scriptos << "plot \\" << endl;
	vector<pair<CCLPointer, string>>::iterator it; // I need it after the cycle
	unsigned int i = 2;
	for (it = CCLAlgorithms.begin(); it != (CCLAlgorithms.end() - 1); ++it, ++i){
		scriptos << "\"" + output_density_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\" , \\" << endl;
	}
	scriptos << "\"" + output_density_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\"" << endl << endl;
	
    scriptos << "# DENSITY GRAPH (BLACK AND WHITE)" << endl << endl;
    
    scriptos << "set output \"" + output_density_graph_bw + "\"" << endl;
    scriptos << "#set title \"Density\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal << " black and white" << endl;
    scriptos << "set terminal " << terminal << " enhanced monochrome dashed font ',15'" << endl << endl;

    scriptos << "replot" << endl << endl;

    // DENSITY NORMALIZED
    scriptos << "#NORMALIZED DENSITY GRAPH (COLORS)" << endl << endl;

    scriptos << "set output \"" + output_normalization_density_graph + "\"" << endl;
    scriptos << "#set title \"Normalized Density\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal << " colors" << endl;
    scriptos << "set terminal " << terminal << " enhanced color font ',15'" << endl << endl;

    scriptos << "# Axes labels" << endl;
    scriptos << "set xlabel \"Density\"" << endl;
    scriptos << "set ylabel \"Normalized Execution Time [ms]\"" << endl << endl;

    scriptos << "# Axes range" << endl;
    scriptos << "set xrange [0:1]" << endl;
    scriptos << "set yrange [*:*]" << endl;
    scriptos << "set logscale y" << endl << endl;

    scriptos << "# Legend" << endl;
    scriptos << "set key left top nobox spacing 2 font ', 8'" << endl << endl;

    scriptos << "# Plot" << endl;
    scriptos << "plot \\" << endl;
    //vector<pair<CCLPointer, string>>::iterator it; // I need it after the cycle
    //unsigned int i = 2;
    i = 2;
    for (it = CCLAlgorithms.begin(); it != (CCLAlgorithms.end() - 1); ++it, ++i){
        scriptos << "\"" + output_density_normalized_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\" , \\" << endl;
    }
    scriptos << "\"" + output_density_normalized_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\"" << endl << endl;

    scriptos << "# NORMALIZED DENSITY GRAPH (BLACK AND WHITE)" << endl << endl;

    scriptos << "set output \"" + output_normalization_density_graph_bw + "\"" << endl;
    scriptos << "#set title \"Density\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal << " black and white" << endl;
    scriptos << "set terminal " << terminal << " enhanced monochrome dashed font ',15'" << endl << endl;

    scriptos << "replot" << endl << endl;

	// SIZE
    scriptos << "# SIZE GRAPH (COLORS)" << endl << endl;

    scriptos << "set output \"" + output_size_graph + "\"" << endl;
    scriptos << "#set title \"Size\" font ',12'" << endl << endl;

    scriptos << "# " << terminal << " colors" << endl;
    scriptos << "set terminal " << terminal << " enhanced color font ',15'" << endl << endl;
    
    scriptos << "# Axes labels" << endl;
    scriptos << "set xlabel \"Pixels\"" << endl;
    scriptos << "set ylabel \"Execution Time [ms]\"" << endl << endl;

    scriptos << "# Axes range" << endl;
    scriptos << "set format x \"10^{%L}\"" << endl;
    scriptos << "set xrange [100:100000000]" << endl;
    scriptos << "set yrange [*:*]" << endl;
    scriptos << "set logscale xy 10" << endl << endl;

    scriptos << "# Legend" << endl;
    scriptos << "set key left top nobox spacing 2 font ', 8'" << endl;

    scriptos << "# Plot" << endl;
	//// Set Labels
	//for (unsigned int i=0; i < supp_size_labels.size(); ++i){
	//	if (supp_size_labels[i].second != 0){
	//		scriptos << "set label " << i+1 << " \"" << supp_size_labels[i].first << "x" << supp_size_labels[i].first << "\" at " << pow(supp_size_labels[i].first,2) << "," << supp_size_labels[i].second << endl;
	//	}
	//	else{
	//		// It means that there is no element with this size characyteristic so this label is not necessary
	//	}
	//}
	//// Set Labels
	scriptos << "plot \\" << endl;
	//vector<pair<CCLPointer, string>>::iterator it; // I need it after the cycle
	//unsigned int i = 2;
	i = 2;
	for (it = CCLAlgorithms.begin(); it != (CCLAlgorithms.end() - 1); ++it, ++i){
		scriptos << "\"" + output_size_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\" , \\" << endl;
	}
	scriptos << "\"" + output_size_result + "\" using 1:" << i << " with linespoints title \"" + (*it).second + "\"" << endl << endl;

    scriptos << "# SIZE (BLACK AND WHITE)" << endl << endl;

    scriptos << "set output \"" + output_size_graph_bw + "\"" << endl;
    scriptos << "#set title \"Size\" font ', 12'" << endl << endl;

    scriptos << "# " << terminal << " black and white" << endl;
    scriptos << "set terminal " << terminal << " enhanced monochrome dashed font ',15'" << endl << endl;

    scriptos << "replot" << endl << endl;

	scriptos << "exit gnuplot" << endl;

	density_os.close();
	size_os.close();
	scriptos.close();
	// GNUPLOT SCRIPT 

    if (0 != std::system(("gnuplot " + complete_output_path + kPathSeparator + gnuplot_script).c_str()))
        return ("Density_Size_Test on '" + input_folder + "': Unable to run gnuplot's script");
	return ("Density_Size_Test on '" + output_folder + "': successfuly done");
}

string memory_test(vector<pair<CCLMemPointer, string>>& CCLMemAlgorithms, Mat1d& algo_averages_accesses, const string& input_path, const string& input_folder, const string& input_txt, string& output_path){

	string output_folder = input_folder,
		   complete_output_path = output_path + kPathSeparator + output_folder;
		   
	uint number_of_decimal_digit_to_display_in_graph = 2;

	// Creation of output path
	if(!makeDir(complete_output_path))
		return ("Memory_Test on '" + input_folder + "': Unable to find/create the output path " + complete_output_path);

	string is_path = input_path + kPathSeparator + input_folder + kPathSeparator + input_txt;

	// For LIST OF INPUT IMAGES
	ifstream is(is_path);
	if (!is.is_open())
		return ("Memory_Test on '" + input_folder + "': Unable to open " + is_path);

	// (TODO move this code into a function)
	// To save list of filename on which CLLAlgorithms must be tested 
	vector<pair<string, bool>> filesNames;  // first: filename, second: state of filename (find or not)
	string filename;
	while (getline(is, filename)){
		// To delete eventual carriage return in the file name (especially designed for windows file newline format) 
		size_t found;
		do{
			// The while cycle is probably unnecessary
			found = filename.find("\r");
			if (found != string::npos)
				filename.erase(found, 1);
		} while (found != string::npos);
		// Add purified file name in the vector
		filesNames.push_back(make_pair(filename, true));
	}
	is.close();
	// (TODO move this code into a function)

	// Number of files
	int fileNumber = filesNames.size();

	// To store averages memory accesses (one column for every data structure type: col 1 -> BINARY_MAT, col 2 -> LABELED_MAT, col 3 -> EQUIVALENCE_VET, col 0 -> OTHER)
	algo_averages_accesses = Mat1d(Size(MD_SIZE, CCLMemAlgorithms.size()), 0);

	// Count number of lines to display "progress bar"
	uint currentNumber = 0;

	uint totTest = 0; // To count the real number of image on which labeling will be applied
	// For every file in list
	for (uint file = 0; file < filesNames.size(); ++file){

		string filename = filesNames[file].first;

		// Display "progress bar"
		if (currentNumber * 100 / fileNumber != (currentNumber - 1) * 100 / fileNumber){
			cout << currentNumber << "/" << fileNumber << "         \r";
			fflush(stdout);
		}
		currentNumber++;

		Mat1b binaryImg;

		if (!getBinaryImage(input_path + kPathSeparator + input_folder + kPathSeparator + filename, binaryImg)){
			if (filesNames[file].second)
				cout << "'" + filename + "' does not exist" << endl;
			filesNames[file].second = false;
			continue;
		}

		totTest++;
		uint i = 0;
		// For all the Algorithms in the list
		for (auto it = CCLMemAlgorithms.begin(); it != CCLMemAlgorithms.end(); ++it, ++i){

			// The following data structure is used to get the memory access matrixes
			vector<unsigned long int> accessesVal; // Rows represents algorithms and columns represent data structures
			uint nLabels;

			nLabels = (*it).first(binaryImg, accessesVal);

			// For every data structure "returned" by the algorithm
			for (size_t a = 0; a < accessesVal.size(); ++a){
				algo_averages_accesses(i, a) += accessesVal[a];
			}
		}// END ALGORITHMS FOR
	} // END FILES FOR

	// To display "progress bar"
	cout << currentNumber << "/" << fileNumber << "         \r";
	fflush(stdout);

	// To calculate average memory accesses
	for (int r = 0; r < algo_averages_accesses.rows; ++r){
		for (int c = 0; c < algo_averages_accesses.cols; ++c){
			algo_averages_accesses(r, c) /= totTest; 
		}
	}
	
	return ("Memory_Test on '" + input_folder + "': successfuly done");
}

// To generate latex table with averages results
void generateLatexTable(const string& output_path, const string& latex_file, const Mat1d& all_res, const vector<string>& algName, const vector<pair<CCLPointer, string>>& CCLAlgorithms){
    
    string latex_path = output_path + kPathSeparator + latex_file; 
    ofstream is(latex_path); 
    if (!is.is_open()){
        cout << "Unable to open/create " + latex_path << endl;
        return;
    }
    
    // fixed number of decimal values
    is << fixed;
    is << setprecision(3);

    is << "%This table format needs the package 'siunitx', please uncomment and add the following line code in latex preamble if you want to add the table in your latex file" << endl; 
    is << "%\\usepackage{siunitx}"<< endl << endl;
    is << "\\begin{table}[tbh]" << endl << endl; 
    is << "\t\\centering" << endl;
    is << "\t\\caption{Average Results in ms (Lower is Better)}" << endl;
    is << "\t\\label{tab:table1}" << endl;
    is << "\t\\begin{tabular}{|l|"; 
    for (uint i = 0; i < CCLAlgorithms.size(); ++i)
        is << "S[table-format=2.3]|"; 
    is << "}" << endl;
    is << "\t\\hline" << endl;
    is << "\t";
    for (uint i = 0; i < CCLAlgorithms.size(); ++i){ 
        string algName = CCLAlgorithms[i].second;
        eraseDoubleEscape(algName);
        //algName.erase(std::remove(algName.begin(), algName.end(), '\\'), algName.end());
        is << " & {" << algName << "}"; //Header
    }
    is << "\\\\" << endl;
    is << "\t\\hline" << endl;
     
    for (uint i = 0; i < algName.size(); ++i){
        is << "\t" << algName[i];
        for (int j = 0; j < all_res.cols; ++j){
            is << " & ";
            if (all_res(i, j) != numeric_limits<double>::max())
                is <<all_res(i, j); //Data
        }
        is << "\\\\" << endl;
    }
    is << "\t\\hline" << endl; 
    is << "\t\\end{tabular}" << endl << endl;
    is << "\\end{table}" << endl;       

    is.close(); 
}

// To generate latex table with memory average accesses
void generateMemoryLatexTable(const string& output_path, const string& latex_file, const Mat1d& accesses,const string& dataset, const vector<pair<CCLMemPointer, string>>& CCLMemAlgorithms){

	// TODO handle if folder does not exists
	string latex_path = output_path + kPathSeparator + dataset + kPathSeparator + latex_file;
	ofstream is(latex_path);
	if (!is.is_open()){
		cout << "Unable to open/create " + latex_path << endl;
		return;
	}

	// fixed number of decimal values
	is << fixed;
	is << setprecision(3);

	is << "%This table format needs the package 'siunitx', please uncomment and add the following line code in latex preamble if you want to add the table in your latex file" << endl;
	is << "%\\usepackage{siunitx}" << endl << endl;
	is << "\\begin{table}[tbh]" << endl << endl;
	is << "\t\\centering" << endl;
	is << "\t\\caption{Analysis of memory accesses required by connected components computation for '" << dataset << "' dataset. The numbers are given in millions of accesses}" << endl;
	is << "\t\\label{tab:table1}" << endl;
	is << "\t\\begin{tabular}{|l|";
	for (int i = 0; i < accesses.cols + 1; ++i)
		is << "S[table-format=2.3]|";
	is << "}" << endl;
	is << "\t\\hline" << endl;
	is << "\t";
	
	// Header
	is << "{Algorithm} & {Binary Image} & {Label Image} & {Equivalence Vector/s}  & {Other} & {Total Accesses}";
	is << "\\\\" << endl;
	is << "\t\\hline" << endl;

	for (uint i = 0; i < CCLMemAlgorithms.size(); ++i){
		
		// For every algorithm
		string algName = CCLMemAlgorithms[i].second;
		eraseDoubleEscape(algName);
		is << "\t{" << algName << "}";

		double tot = 0; 

		for (int s = 0; s < accesses.cols; ++s){			
			// For every data structure
			if (accesses(i, s) != 0)
				is << "\t& " << (accesses(i, s) / 1000000);
			else
				is << "\t& "; 

			tot += (accesses(i, s) / 1000000); 
		}
		// Total Accesses
		is << "\t& " << tot; 

		// EndLine
		is << "\t\\\\" << endl;
	}

	// EndTable
	is << "\t\\hline" << endl;
	is << "\t\\end{tabular}" << endl << endl;
	is << "\\end{table}" << endl;

	is.close();
}


int main(int argc, char **argv) 
{
    // Configuration file
	ConfigFile cfg("config.cfg");  

    // Flags to customize output format
    bool output_colors_density_size = cfg.getValueOfKey<bool>("ds_colorLabels", false),
         output_colors_average_test = cfg.getValueOfKey<bool>("at_colorLabels", false),
         write_n_labels = cfg.getValueOfKey<bool>("write_n_labels", true),
         check_8connectivity = cfg.getValueOfKey<bool>("check_8connectivity", true),
         ds_saveMiddleTests = cfg.getValueOfKey<bool>("ds_saveMiddleTests", false),
         at_saveMiddleTests = cfg.getValueOfKey<bool>("at_saveMiddleTests", false),
         ds_perform = cfg.getValueOfKey<bool>("ds_perform", true),
         at_perform = cfg.getValueOfKey<bool>("at_perform", true),
		 mt_perform = cfg.getValueOfKey<bool>("mt_perform", true);

    // Number of tests
    uint8_t ds_testsNumber = cfg.getValueOfKey<uint>("ds_testsNumber", 1), 
            at_testsNumber = cfg.getValueOfKey<uint>("at_testsNumber", 1);

	string input_txt = "files.txt",             /* Files who contains list of images's name on which CCLAlgorithms are tested */
           gnuplot_scipt_extension = ".gnuplot",  /* Extension of gnuplot scripts*/
           colors_folder = "colors",
           middel_folder = "middle_results",
           latec_file = "averageResults.tex",
		   latex_memory_file = "memoryAccesses.tex",
           output_path = cfg.getValueOfKey<string>("output_path", "output"), /* Folder on which result are stored */
           input_path = cfg.getValueOfKey<string>("input_path", "input");    /* Folder on which datasets are placed */
               
    // List of dataset on which CCLA are checked
	vector<string> check_list = cfg.getStringValuesOfKey("check_list", vector<string> {"3dpes", "fingerprints", "hamlet", "medical", "mirflickr", "test_random", "tobacco800"});

	// List of dataset on which CCLA are memory checked
	vector<string> memory_list = cfg.getStringValuesOfKey("memory_tests", vector<string> {"3dpes", "fingerprints", "hamlet", "medical", "mirflickr", "test_random", "tobacco800"});

    // Lists of dataset on which CCLA are tested: one list for every type of test
    vector<string> input_folders_density_size_test = { "test_random" },
				   input_folders_averages_test = cfg.getStringValuesOfKey("averages_tests" , vector<string> {"3dpes", "fingerprints", "hamlet", "medical", "mirflickr", "tobacco800"});

    // Lists of 'STANDARD' algorithms to check and/or test
    vector<pair<CCLPointer, string>> CCLAlgorithms;
    vector<string> funcName = cfg.getStringValuesOfKey("CCLAlgoFunc", vector<string> {});
    vector<string> algName = cfg.getStringValuesOfKey("CCLAlgoName", vector<string> {});

    if (funcName.size() != algName.size() || funcName.size() == 0)
    {
        cout << "'CCLAlgorithmsFunc' and 'CCLAlgorithmsName' must match in length and order and must not be empty" << endl;
        return 1; 
    }

    uint i = 0; 
    for (vector<string>::iterator it = funcName.begin(); it != funcName.end(); ++it, ++i){  
        if (CCLAlgorithmsMap.find(*it) == CCLAlgorithmsMap.end())
            cout << "Unable to find '" << *it << "' algorithm, skipped" << endl;
        else
            CCLAlgorithms.push_back({CCLAlgorithmsMap.find(*it)->second, algName[i]});
    }
	// Lists of 'STANDARD' algorithms to check and/or test

	// Lists of 'MEMORY' algorithms on which execute memory test
	vector<pair<CCLMemPointer, string>> CCLMemAlgorithms;
	vector<string> funcMemName = cfg.getStringValuesOfKey("CCLMemAlgoFunc", vector<string> {});
	vector<string> algoMemName = cfg.getStringValuesOfKey("CCLMemAlgoName", vector<string> {});

	if (mt_perform && (funcMemName.size() != algoMemName.size() || funcMemName.size() == 0))
	{
		cout << "'CCLMemAlgorithmsFunc' and 'CCLMemAlgorithmsName' must match in length and order and must not be empty. Please check this or set 'mt_perform' flag to false to skip memory tests" << endl;
		return 1;
	}

	i = 0;
	for (vector<string>::iterator it = funcMemName.begin(); it != funcMemName.end(); ++it, ++i){
		if (CCLMemAlgorithmsMap.find(*it) == CCLMemAlgorithmsMap.end())
			cout << "Unable to find '" << *it << "' algorithm, skipped" << endl;
		else
			CCLMemAlgorithms.push_back({ CCLMemAlgorithmsMap.find(*it)->second, algoMemName[i] });
	}
	// Lists of 'MEMORY' algorithms on which execute memory test

    // Create output directory
   if(!makeDir(output_path))
	   return 1;

	// Check if algorithms are correct
    //if (check_8connectivity){
   if (true) {
        cout << "CHECK ALGORITHMS ON 8-CONNECTIVITY: " << endl;
		if (CCLAlgorithms.size() == 0){
			cout << "ERROR: no algorithms, check skipped" << endl; 
		}
		else{
			checkAlgorithms(CCLAlgorithms, check_list, input_path, input_txt);
		}
    }
	// Check if algorithms are correct

	// Test Algorithms with different input type and different output format, and show execution result
	// AVERAGES TEST
    Mat1d all_res(input_folders_averages_test.size(), CCLAlgorithms.size(), numeric_limits<double>::max()); // We need it to save average results and generate latex table
    if (at_perform){
        cout << endl << "AVERAGE TESTS: " << endl;
		if (CCLAlgorithms.size() == 0){
			cout << "ERROR: no algorithms, average tests skipped" << endl;
		}
		else{
			for (unsigned int i = 0; i < input_folders_averages_test.size(); ++i){
	    		cout << "Averages_Test on '" << input_folders_averages_test[i] << "': starts" << endl;
				cout << averages_test(CCLAlgorithms, all_res, i, input_path, input_folders_averages_test[i], input_txt, gnuplot_scipt_extension, output_path, colors_folder, at_saveMiddleTests, at_testsNumber, middel_folder, write_n_labels, output_colors_average_test) << endl;
	    		cout << "Averages_Test on '" << input_folders_averages_test[i] << "': ends" << endl << endl;
			}
        generateLatexTable(output_path, latec_file, all_res, input_folders_averages_test, CCLAlgorithms);
		}
	}
    
	// DENSITY_SIZE_TESTS
    if (ds_perform){
        cout << endl << "DENSITY_SIZE TESTS: " << endl;
		if (CCLAlgorithms.size() == 0){
			cout << "ERROR: no algorithms, density_size tests skipped" << endl;
		}
		else{
			for (unsigned int i = 0; i < input_folders_density_size_test.size(); ++i){
				cout << "Density_Size_Test on '" << input_folders_density_size_test[i] << "': starts" << endl;
				cout << density_size_test(CCLAlgorithms, input_path, input_folders_density_size_test[i], input_txt, gnuplot_scipt_extension, output_path, colors_folder, ds_saveMiddleTests, ds_testsNumber, middel_folder, write_n_labels, output_colors_density_size) << endl;
				cout << "Density_Size_Test on '" << input_folders_density_size_test[i] << "': ends" << endl << endl;
			}
		}
    }

	// MEMORY_TESTS
	//if (mt_perform){
	if (true) {
		Mat1d accesses; 
		cout << endl << "MEMORY TESTS: " << endl;
		if (CCLMemAlgorithms.size() == 0){
			cout << "ERROR: no algorithms, memory tests skipped" << endl;
		}
		else{
			for (unsigned int i = 0; i < memory_list.size(); ++i){
				cout << "Memory_Test on '" << memory_list[i] << "': starts" << endl;
				cout << memory_test(CCLMemAlgorithms, accesses, input_path, memory_list[i], input_txt, output_path) << endl;
				cout << "Memory_Test on '" << memory_list[i] << "': ends" << endl << endl;
				generateMemoryLatexTable(output_path, latex_memory_file, accesses, memory_list[i], CCLMemAlgorithms);
			}
		}
	}

	return 0; 
}
	
