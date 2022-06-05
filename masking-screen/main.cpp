#include <stdio.h>
#include <iostream>
#include <crtdbg.h>
#include <Windows.h>
#include <fstream>
#include <thread>
#include <mutex>
#include <future>
#include <semaphore>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <queue>
#include <filesystem>
#include <regex>
#include <vector>

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>


constexpr int BIT_COUNT = 24;
constexpr int NUM_OCR_THREADS = 14;

int monitor_selected = 0;
int monitorCount = 0;
int monitorWidth = 0;
int monitorHeight = 0;
int monitorLeft = 0;
int monitorTop = 0;

using namespace std;
using namespace concurrency;

struct compare {
    bool operator()(pair<int, cv::Mat>& frame1, pair<int, cv::Mat>& frame2) {
        return frame1.first > frame2.first;
    }
};

/** @brief ��������
*   bs_input, bs_output: �̹��� ����� ť�� ���̽� ����� ����
*   bs_end: �޸� �뷮 ����
*
*   input_queue: �̹��� �Է�/ó�� ť
*   output_queue: ��� �̹��� �Է�/��� ť
*
*   currentFrameCounter: ������� ��µ� �̹��� ����
*/
std::binary_semaphore semInputQueue(1);
std::binary_semaphore semOutputQueue(1);
std::binary_semaphore semCapture(1);

queue<pair<int, cv::Mat>> inputQueue;
priority_queue<pair<int, cv::Mat>, vector<pair<int, cv::Mat>>, compare> outputQueue;

int currentFrameCounter = 1;

std::vector<std::regex> _reList;
std::vector<std::string> _reStrList;
std::vector<std::string> _incList;
std::vector<std::string> _excList;


/** @brief �־��� ���Խ�, ����� ���� ����, ���� �ܾ� ����Ʈ�� ������� ���������� ����.
*
*   @param[in]  word        �������� ���� ��� �ܾ�
*   @param[in]  reList      �������� ���⿡ ����� ���Խ� ����Ʈ
*   @param[in]  reStrList   ���Խ� ����Ʈ�� �����ϴ� ���Խ� ���ڿ� ����Ʈ
*   @param[in]  incList     �������� ���⿡ ����� ����� ���� ���� �ܾ� ����Ʈ
*   @param[in]  excList     �������� ���⿡ ����� ����� ���� ���� �ܾ� ����Ʈ
*   @return     bool        �������� ���⿡ �����Ͽ��ٸ� true, �����Ͽ��ٸ� false ��ȯ
*/
bool CheckPI(std::string word,
    std::vector<std::regex>& reList,
    std::vector<std::string> reStrList,
    std::vector<std::string>& incList,
    std::vector<std::string>& excList) {

    //����� ���� ���� �ܾ� ����Ʈ�� �������� �������� ����
    for (int i = 0; i < incList.size(); i++) {
        //�����Ѵٸ�, true ��ȯ
        if ((word.find(incList.at(i)) != std::string::npos)) {
            return true;
        }
    }

    //����� ���� ���� �ܾ� ����Ʈ�� �������� �������� ����
    for (int i = 0; i < excList.size(); i++) {
        //�����Ѵٸ�, false ��ȯ
        if ((word.find(excList.at(i)) != std::string::npos)) {
            return false;
        }
    }

    //���Խ� ����Ʈ�� �������� �������� ����
    std::smatch match;
    for (int i = 0; i < reList.size(); i++) {
        //�����Ѵٸ�, true ��ȯ
        if (std::regex_search(word, match, reList.at(i))) {
            std::cout << reStrList[i] << " ���� ��ġ\n";
            return true;
        }
    }

    //��� ������ �������� ���� �õ����� ����� ���� ���, false ��ȯ
    return false;
}


/** @brief �������� ���⿡ ����� ���Խ� ����Ʈ �ʱ�ȭ
*
*   @param[in]  db          �������� ���⿡ ����� ���Խ��� �����ϴ� �����ͺ��̽�
*   @param[out] reList      �������� ���⿡ ����� ���Խ� ����Ʈ
*   @param[out] reStrList   ���Խ� ����Ʈ�� �����ϴ� ���Խ� ���ڿ� ����Ʈ
*   @return     void
*/
void InitPICheckerRegex(SQLite::Database* db, std::vector<std::regex>& reList, std::vector<std::string>& reStrList) {
    std::vector<std::string> regexInputList;

    SQLite::Statement cmd(*db, "SELECT * FROM regex");
    while (cmd.executeStep())
    {
        regexInputList.push_back(cmd.getColumn(0));
    }

    std::vector<std::string>::iterator it;
    for (it = regexInputList.begin(); it != regexInputList.end(); ++it) {
        try {
            reList.push_back(std::regex(*it));
            reStrList.push_back(*it);
        }
        catch (std::exception& e) {
            std::cout << *it << "���� ���Խ� ��ü ���� ����\n";
            std::cout << e.what() << "\n\n";
        }
    };
}


/** @brief �������� ���⿡ ����� ����� ���� ����, ���� �ܾ� ����Ʈ �ʱ�ȭ
*
*   @param[in]  db          �������� ���⿡ ����� ����� ���� ����, ���� �ܾ �����ϴ� �����ͺ��̽�
*   @param[out] incList     �������� ���⿡ ����� ���Խ� ����Ʈ
*   @param[out] excList     ���Խ� ����Ʈ�� �����ϴ� ���Խ� ���ڿ� ����Ʈ
*   @return     void
*/
void InitPICheckerUser(SQLite::Database* db, std::vector<std::string>& incList, std::vector<std::string>& excList) {
    //userdefined_include �ʱ�ȭ
    SQLite::Statement cmd1(*db, "SELECT * FROM userdefined_include");
    while (cmd1.executeStep())
    {
        incList.push_back(cmd1.getColumn(0));
    }

    //userdefined_exclude �ʱ�ȭ
    SQLite::Statement cmd2(*db, "SELECT * FROM userdefined_exclude");
    while (cmd2.executeStep())
    {
        excList.push_back(cmd2.getColumn(0));
    }
}


/** @brief ĸó�� ȭ�� �̹����κ��� OCR �۾�, �������� �Ǻ�, ����ŷ �۾� ����.
*
*   @param[in]  api         tesseract OCR �۾� �ڵ鷯
*   @return     void
*/
void FilterScreenPI(tesseract::TessBaseAPI* api, int resizeHeight) {
    //���̽� ����� ������ ���� ���� ���� �����ð� ����
    Sleep(rand() % 100 + 1);

    while (true)
    {
        semInputQueue.acquire();
        if (!inputQueue.empty()) {
            int frameCounter = inputQueue.front().first;
            cv::Mat originImage = inputQueue.front().second;
            if (originImage.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();

            //��������� �̹��� ��ǥ ���� ���� ���� ���
            double imageRatio = originImage.rows / (double)originImage.cols;
            double multipleRatio = originImage.rows / (double)resizeHeight;

            //�̹��� ��ó��: ��������, ���ó��, ����ó��
            cv::Mat ocrImage;
            cv::resize(originImage, ocrImage, cv::Size(resizeHeight / imageRatio, resizeHeight), 0, 0, cv::INTER_CUBIC);
            cv::cvtColor(ocrImage, ocrImage, cv::COLOR_BGR2GRAY);
            cv::adaptiveThreshold(ocrImage, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 11, 0);

            //OCR �̹��� ���� �� ����
            api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
            api->Recognize(0);

            //�ܾ� ���� �ν�
            tesseract::ResultIterator* ri = api->GetIterator();
            tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
            if (ri != 0) {
                do {
                    float conf = ri->Confidence(level);
                    if (conf > 30) {
                        string word = ri->GetUTF8Text(level);

                        // �������� ���� ��, �ش� ���� ����ŷ
                        if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            cv::rectangle(
                                originImage,
                                cv::Rect(
                                    cv::Point(x1 * multipleRatio, y1 * multipleRatio),
                                    cv::Point(x2 * multipleRatio, y2 * multipleRatio)),
                                cv::Scalar(0, 0, 0),
                                -1);
                        }
                    }
                } while (ri->Next(level));
            }
            semOutputQueue.acquire();
            outputQueue.push(make_pair(frameCounter, originImage));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }

        //�޸� �뷮 ���� ����
        //�������� ó�� �۾��� ���� ��, ĸó ����
        semCapture.release();
    }
    return;
}


/** @brief ĸó�� ȭ�� �̹����κ��� OCR �۾�, �������� �Ǻ�, ����ŷ �۾� ����, ���� ���� ��� ����
*
*   @param[in]  api         tesseract OCR �۾� �ڵ鷯
*   @return     void
*/
void FilterScreenPI_BUFFER(tesseract::TessBaseAPI* api) {
    //���̽� ����� ������ ���� ���� ���� �����ð� ����
    Sleep(rand() % 100 + 1);

    while (true)
    {
        semInputQueue.acquire();
        if (!inputQueue.empty()) {
            int frameCounter = inputQueue.front().first;
            cv::Mat originImage = inputQueue.front().second;
            if (originImage.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();


            //�̹��� ��ó��: ���ó��, ����ó��
            cv::Mat ocrImage = originImage.clone();
            cv::cvtColor(ocrImage, ocrImage, cv::COLOR_BGR2GRAY);
            //cv::adaptiveThreshold(ocrImage, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 7, 0);

            //OCR �̹��� ���� �� ����
            api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
            api->Recognize(0);

            //�ܾ� ���� �ν�
            tesseract::ResultIterator* ri = api->GetIterator();
            tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
            if (ri != 0) {
                do {
                    float conf = ri->Confidence(level);
                    if (conf > 50) {
                        string word = ri->GetUTF8Text(level);

                        // �������� ���� ��, �ش� ���� ����ŷ
                        if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                            printf("%s\n", word);
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            cv::rectangle(
                                originImage,
                                cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(0, 0, 0), -1);
                        }
                    }
                } while (ri->Next(level));
            }
            semOutputQueue.acquire();
            outputQueue.push(make_pair(frameCounter, originImage));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }
    }
    return;
}


bool FilterCameraPI_DEBUG(tesseract::TessBaseAPI* api) {
    srand(time(0));
    std::stringstream ss;
    ss << std::this_thread::get_id();
    uint64_t id = std::stoull(ss.str());
    int random_time = rand() % id % 500 + 1;
    Sleep(random_time);
    while (true)
    {
        semInputQueue.acquire();
        //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
        if (!inputQueue.empty()) {
            int frame_counter = inputQueue.front().first;
            cv::Mat origin_image_clone = inputQueue.front().second;
            if (origin_image_clone.empty()) {
                inputQueue.pop();
                semInputQueue.release();
                continue;
            }
            inputQueue.pop();
            semInputQueue.release();

            cv::Mat ocr_image;
            double aspect_ratio = origin_image_clone.rows / (double)origin_image_clone.cols;
            int resize_image_height = 1600;
            double multiple_ratio = origin_image_clone.rows / (double)resize_image_height;
            cv::resize(origin_image_clone, ocr_image, cv::Size(resize_image_height / aspect_ratio, resize_image_height), 0, 0, cv::INTER_CUBIC);
            cv::cvtColor(ocr_image, ocr_image, cv::COLOR_BGR2GRAY);
            cv::adaptiveThreshold(ocr_image, ocr_image, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 3, 5);

            semOutputQueue.acquire();
            outputQueue.push(make_pair(frame_counter, ocr_image));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }

        semCapture.release();
    }
}


/** @brief ���� ���÷��̷� ����ŷ ó���� �̹��� ���.
*
*   @return       void
*/
void ExportVirtualScreen() {
    //������ ��ǥ�� �����ϴ� ���� ���÷��̿� ��ü ȭ������ ���Բ� â �Ӽ� ����
    string wName = "Press ESC to stop.";
    cv::namedWindow(wName, cv::WINDOW_NORMAL);
    cv::moveWindow(wName, monitorLeft + monitorWidth + 1, monitorTop - 1);
    cv::setWindowProperty(wName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //���� ���÷��̷� �̹��� ���
                cv::imshow(wName, image);
                currentFrameCounter++;

                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return;
}


/** @brief ���� ���÷��̷� ����ŷ ó���� �̹��� ���, ���� ���� ��� ����
*
*   @return       void
*/
void ExportVirtualScreen_BUFFER(int fps, int bufferSize) {
    //������ ��ǥ�� �����ϴ� ���� ���÷��̿� ��ü ȭ������ ���Բ� â �Ӽ� ����
    string wName = "Press ESC to stop.";
    cv::namedWindow(wName, cv::WINDOW_NORMAL);
    cv::moveWindow(wName, monitorLeft + monitorWidth + 1, monitorTop - 1);
    cv::setWindowProperty(wName, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    bool queueSizeFlag = false;
    while (true)
    {
        //printf("outputQueue.size()=%zd\n", outputQueue.size());
        if (!queueSizeFlag) {
            if (outputQueue.size() > bufferSize) {
                queueSizeFlag = true;
            }
            continue;
        }

        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //���� ���÷��̷� �̹��� ���
                cv::imshow(wName, image);
                Sleep(1000 / fps);
                currentFrameCounter++;

                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return;
}

/***
* �׽�Ʈ��
*/
bool do_show_fake() {
    string window_name = "Press ESC to stop.";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    //cv::moveWindow(window_name, monitor_capture_left + monitor_capture_width + 1, monitor_capture_top - 1);
    //cv::setWindowProperty(window_name, cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                //printf("cur=%d\tqueue=%d\n", current_frame_counter, output_queue.top().first);
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();
                cv::imshow(window_name, image);
                //printf("cols=%d\trows=%d\n", image.cols, image.rows);
                currentFrameCounter++;
                if (cv::waitKey(1) == 27)
                {
                    cout << "ESC hit!" << endl;
                    cv::destroyAllWindows();
                    break;
                }
            }
            else {
                semOutputQueue.release();
            }
        }
        else {
            semOutputQueue.release();
        }
    }
    return false;
}

/** @brief  �� ���÷��̿��� �̹��� ĸó.
*
*   @param[in]  fCounter    ������� ĸó�� �̹��� ����
*   @param[in]  width       ĸó ���÷��� ���, �ʺ�
*   @param[in]  height      ĸó ���÷��� ���, ����
*   @param[in]  bi          ĸó ��Ʈ�� �̹��� ���
*   @return     void
*/
void CaptureScreen(int* fCounter, int width, int height, BITMAPINFOHEADER bi) {
    while (true)
    {
        //����� �޸� �ִ� �뷮 ����
        //!TODO: �����ص� �� ��� ������? ���� �ʿ�
        if (inputQueue.size() > 512) {
            continue;
        }

        //�ڵ鷯 ����
        HDC hScreen = GetDC(NULL);
        HDC hDC = CreateCompatibleDC(hScreen);

        //ĸó �� �޸𸮿� ������ ��Ʈ�� �̹��� ����
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        HGDIOBJ old_obj = SelectObject(hDC, hBitmap);

        //��Ʈ�� �̹��� ���·� ���÷��� ĸó
        BOOL bRet = BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

        //OpenCV �̹����� ��ȯ
        cv::Mat mat;
        mat.create(height, width, CV_8UC3);
        GetDIBits(hDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        //���ҽ� ���� �� ȸ��
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, mat));
        (*fCounter)++;
        semInputQueue.release();

        //�޸� �뷮 ���� ����
        //�������� ó�� �����尡 ���� ��, semCapture�� ������
        semCapture.acquire();
    }
    return;
}


/** @brief  �� ���÷��̿��� �̹��� ĸó, ���� ���� ��� ����
*
*   @param[in]  fCounter    ������� ĸó�� �̹��� ����
*   @param[in]  width       ĸó ���÷��� ���, �ʺ�
*   @param[in]  height      ĸó ���÷��� ���, ����
*   @param[in]  bi          ĸó ��Ʈ�� �̹��� ���
*   @return     void
*/
void CaptureScreen_BUFFER(int* fCounter, int width, int height, BITMAPINFOHEADER bi, int fps) {
    while (true)
    {
        //�ڵ鷯 ����
        HDC hScreen = GetDC(NULL);
        HDC hDC = CreateCompatibleDC(hScreen);

        //ĸó �� �޸𸮿� ������ ��Ʈ�� �̹��� ����
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        HGDIOBJ old_obj = SelectObject(hDC, hBitmap);

        //��Ʈ�� �̹��� ���·� ���÷��� ĸó
        BOOL bRet = BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

        //OpenCV �̹����� ��ȯ
        cv::Mat mat;
        mat.create(height, width, CV_8UC3);
        GetDIBits(hDC, hBitmap, 0, height, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

        //���ҽ� ���� �� ȸ��
        SelectObject(hDC, old_obj);
        DeleteDC(hDC);
        ReleaseDC(NULL, hScreen);
        DeleteObject(hBitmap);

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, mat));
        (*fCounter)++;
        semInputQueue.release();
        Sleep(1000 / fps);
    }
    return;
}


/** @brief  ����� ��� ȹ��� �ݹ� �Լ�.
*   ������� �ʺ�, ����, �»�� ��ǥ ȹ��
*   EnumDisplayMonitors�� �ݹ� �Լ��� ���
*/
BOOL CALLBACK MonitorEnumProc(HMONITOR hmonitor, HDC hdc, LPRECT lprect, LPARAM lpara) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hmonitor, &mi);
    if (monitor_selected == monitorCount) {
        monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
        monitorLeft = mi.rcMonitor.left;
        monitorTop = mi.rcMonitor.top;
        std::cout << "ĸó�� ����� �ػ�: " << monitorWidth << "*" << monitorHeight << std::endl;
        std::cout << "ĸó�� ����� �»�� ��ǥ: (" << monitorLeft << ", " << monitorTop << ")" << std::endl;
    }
    monitorCount++;
    return true;
}


int main(int argc, char* argv[])
{
    // C:\Users\Eungyu\source\repos\miniprj-masking-pi\external\share\tessdata
    if (!argv[1]) {
        std::cout << "�����ͼ� ������ �����ϴ� ��θ� ù��° ���ڿ� �Է����ּ���." << endl;
        return 1;
    }
    const char* TESSDATA_PATH = argv[1];

    if (!argv[2]) {
        std::cout << "����� ���� ������ ������ �ι�° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }

    char* p;
    long _bufferSize = strtol(argv[2], &p, 10);
    if (*p != '\0') {
        std::cout << "����� ���� ������ ������ �ι�° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }


    if (!argv[3]) {
        std::cout << "����� �����ͺ��̽� ��θ� ����° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }

    SQLite::Database db(argv[3], SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    InitPICheckerRegex(&db, _reList, _reStrList);
    InitPICheckerUser(&db, _incList, _excList);


    monitor_selected = 0;
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, NULL);

    // ĸó�� ��ũ�� ũ�� ����� ����
    RECT tScreen;

    // ����ũž �ڵ鷯 ���� �� ��ũ�� ũ�� ������
    const HWND hDesktop = GetDesktopWindow();
    GetWindowRect(hDesktop, &tScreen);

    int width = tScreen.right;
    int height = tScreen.bottom;

    // ����� �̹��� ũ�� ����
    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = BIT_COUNT;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    tesseract::TessBaseAPI* api1 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api2 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api3 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api4 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api5 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api6 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api7 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api8 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api9 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api10 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api11 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api12 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api13 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api14 = new tesseract::TessBaseAPI();
    tesseract::TessBaseAPI* api15 = new tesseract::TessBaseAPI();


    /**
    * tesseract::OEM_DEFAULT
    * tesseract::OEM_LSTM_ONLY
    * tesseract::OEM_TESSERACT_ONLY
    * tesseract::OEM_TESSERACT_LSTM_COMBINED
    */
    if (api1->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        std::cout << "�Էµ� ��� " << argv[1] << "�� �����ͼ� ������ �������� �ʽ��ϴ�." << endl;
        std::cout << "���� ��ũ���� �����ͼ��� �ٿ�ε� ��������." << endl;
        std::cout << "eng: https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata" << endl;
        std::cout << "kor: https://github.com/tesseract-ocr/tessdata_fast/raw/main/kor.traineddata" << endl;
        return 1;
    }
    if (api2->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api3->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api4->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api5->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api6->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api7->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api8->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api9->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api10->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api11->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api12->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api13->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api14->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }
    if (api15->Init(TESSDATA_PATH, "eng+kor", tesseract::OEM_LSTM_ONLY)) {
        return 1;
    }

    /**
    * tesseract::PSM_SINGLE_BLOCK
    * ...
    */
    tesseract::PageSegMode PSM_MODE = static_cast<tesseract::PageSegMode>(tesseract::PSM_SINGLE_BLOCK);
    api1->SetPageSegMode(PSM_MODE);
    api2->SetPageSegMode(PSM_MODE);
    api3->SetPageSegMode(PSM_MODE);
    api4->SetPageSegMode(PSM_MODE);
    api5->SetPageSegMode(PSM_MODE);
    api6->SetPageSegMode(PSM_MODE);
    api7->SetPageSegMode(PSM_MODE);
    api8->SetPageSegMode(PSM_MODE);
    api9->SetPageSegMode(PSM_MODE);
    api10->SetPageSegMode(PSM_MODE);
    api11->SetPageSegMode(PSM_MODE);
    api12->SetPageSegMode(PSM_MODE);
    api13->SetPageSegMode(PSM_MODE);
    api14->SetPageSegMode(PSM_MODE);
    api15->SetPageSegMode(PSM_MODE);

    int resizeHeight = height;
    int frame_counter = 1;
    int fps = 10;
    int bufferSize = (int)_bufferSize;

    //ĸó ������ ����, �񵿱�
    std::future<void> thread_capture = std::async(
        std::launch::async,
        [&frame_counter, width, height, bi, fps]() {
            CaptureScreen_BUFFER(&frame_counter, width, height, bi, fps);
        });

    //�������� ó�� ������ ����, �񵿱�
    //!TODO: [������ CPU�� ������ ���� - 1]��ŭ ������ ����
    auto ocr1 = std::async(std::launch::async, [api1]() { FilterScreenPI_BUFFER(api1); });
    auto ocr2 = std::async(std::launch::async, [api2]() { FilterScreenPI_BUFFER(api2); });
    auto ocr3 = std::async(std::launch::async, [api3]() { FilterScreenPI_BUFFER(api3); });
    auto ocr4 = std::async(std::launch::async, [api4]() { FilterScreenPI_BUFFER(api4); });
    auto ocr5 = std::async(std::launch::async, [api5]() { FilterScreenPI_BUFFER(api5); });
    auto ocr6 = std::async(std::launch::async, [api6]() { FilterScreenPI_BUFFER(api6); });
    auto ocr7 = std::async(std::launch::async, [api7]() { FilterScreenPI_BUFFER(api7); });
    auto ocr8 = std::async(std::launch::async, [api8]() { FilterScreenPI_BUFFER(api8); });
    auto ocr9 = std::async(std::launch::async, [api9]() { FilterScreenPI_BUFFER(api9); });
    auto ocr10 = std::async(std::launch::async, [api10]() { FilterScreenPI_BUFFER(api10); });
    auto ocr11 = std::async(std::launch::async, [api11]() { FilterScreenPI_BUFFER(api11); });
    auto ocr12 = std::async(std::launch::async, [api12]() { FilterScreenPI_BUFFER(api12); });
    auto ocr13 = std::async(std::launch::async, [api13]() { FilterScreenPI_BUFFER(api13); });
    auto ocr14 = std::async(std::launch::async, [api14]() { FilterScreenPI_BUFFER(api14); });
    auto ocr15 = std::async(std::launch::async, [api15]() { FilterScreenPI_BUFFER(api15); });

    //���� ī�޶� ��� ������ ����, �񵿱�
    std::future<void> thread_show = std::async(
        std::launch::async, [fps, bufferSize]() {
            ExportVirtualScreen_BUFFER(fps, bufferSize);
        });

    /*
    auto ocr8 = std::async(std::launch::async, [api8, resizeHeight]() { FilterScreenPI(api8, resizeHeight); });
    auto ocr9 = std::async(std::launch::async, [api9, resizeHeight]() { FilterScreenPI(api9, resizeHeight); });
    auto ocr10 = std::async(std::launch::async, [api10, resizeHeight]() { FilterScreenPI(api10, resizeHeight); });
    auto ocr11 = std::async(std::launch::async, [api11, resizeHeight]() { FilterScreenPI(api11, resizeHeight); });
    auto ocr12 = std::async(std::launch::async, [api12, resizeHeight]() { FilterScreenPI(api12, resizeHeight); });
    auto ocr13 = std::async(std::launch::async, [api13, resizeHeight]() { FilterScreenPI(api13, resizeHeight); });
    auto ocr14 = std::async(std::launch::async, [api14, resizeHeight]() { FilterScreenPI(api14, resizeHeight); });
    auto ocr15 = std::async(std::launch::async, [api15, resizeHeight]() { FilterScreenPI(api15, resizeHeight); });
    */

    /*
    auto ocr1 = std::async(std::launch::async, [api1]() { do_OCR_fake(api1); });
    auto ocr2 = std::async(std::launch::async, [api2]() { do_OCR_fake(api2); });
    auto ocr3 = std::async(std::launch::async, [api3]() { do_OCR_fake(api3); });
    auto ocr4 = std::async(std::launch::async, [api4]() { do_OCR_fake(api4); });
    auto ocr5 = std::async(std::launch::async, [api5]() { do_OCR_fake(api5); });
    auto ocr6 = std::async(std::launch::async, [api6]() { do_OCR_fake(api6); });
    auto ocr7 = std::async(std::launch::async, [api7]() { do_OCR_fake(api7); });
    auto ocr8 = std::async(std::launch::async, [api8]() { do_OCR_fake(api8); });
    */

    _CrtDumpMemoryLeaks();
    return 0;
}