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
#include <dshow.h>
#include <regex>
#include <vector>

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <pyvirtualcam/native_windows_unity_capture/virtual_output.h>
#include <leptonica/allheaders.h>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>

#pragma comment(lib, "strmiids")

constexpr int BIT_COUNT = 24;
constexpr int NUM_OCR_THREADS = 14;

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


double GetCosineValue(cv::Point pt1, cv::Point pt2, cv::Point pt0)
{
    double dx1 = pt1.x - pt0.x;
    double dy1 = pt1.y - pt0.y;
    double dx2 = pt2.x - pt0.x;
    double dy2 = pt2.y - pt0.y;
    return (dx1 * dx2 + dy1 * dy2) / sqrt((dx1 * dx1 + dy1 * dy1) * (dx2 * dx2 + dy2 * dy2) + 1e-10);
}

HRESULT EnumerateDevices(REFGUID category, IEnumMoniker** ppEnum)
{
    // Create the System Device Enumerator.
    ICreateDevEnum* pDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDevEnum));

    if (SUCCEEDED(hr))
    {
        // Create an enumerator for the category.
        hr = pDevEnum->CreateClassEnumerator(category, ppEnum, 0);
        if (hr == S_FALSE)
        {
            hr = VFW_E_NOT_FOUND;  // The category is empty. Treat as an error.
        }
        pDevEnum->Release();
    }
    return hr;
}


/** @brief  ��� ������ ī�޶� ���� ȹ��
*   ���� ��� ������ ī�޶� �̸��� STDOUT���� ���
*
*   @param[in]  pEnum       EnumerateDevices�� ����� Device Enumerator
*   @return     void
*/
void DisplayDeviceInformation(IEnumMoniker* pEnum)
{
    IMoniker* pMoniker = NULL;

    int num = 0;
    while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
    {
        IPropertyBag* pPropBag;
        HRESULT hr = pMoniker->BindToStorage(0, 0, IID_PPV_ARGS(&pPropBag));
        if (FAILED(hr))
        {
            pMoniker->Release();
            continue;
        }

        VARIANT var;
        VariantInit(&var);

        hr = pPropBag->Read(L"Description", &var, 0);
        if (FAILED(hr))
        {
            hr = pPropBag->Read(L"FriendlyName", &var, 0);
        }
        if (SUCCEEDED(hr))
        {
            printf("[%d]: %S@#", num++, var.bstrVal);
            VariantClear(&var);
        }

        hr = pPropBag->Write(L"FriendlyName", &var);

        pPropBag->Release();
        pMoniker->Release();
    }
}


/** @brief ���ڷ� �־��� �̹����κ��� ���簢�� �ν��� ����, ��ǥ�� ����Ʈ ���·� ��ȯ.
*
*   @param[in]  img         ���簢�� �ν��� ������ �̹���
*   @return     squareList  �νĵ� ���簢���� ���ϴ�, ���� ��ǥ Ʃ�� ����Ʈ
*/
vector<pair<cv::Point, cv::Point>> GetSquareList(cv::Mat image) {
    const int threshCanny = 1;
    vector<pair<cv::Point, cv::Point>> squareList;

    //��� ó��
    cv::Mat grayImage;
    cv::cvtColor(image, grayImage, cv::COLOR_BGR2GRAY);

    //ADAPTIVE_THRESH_GAUSSIAN ��� ����ȭ ó��
    cv::Mat threImage;
    cv::adaptiveThreshold(grayImage, threImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 61, 7);

    //Ȯ�� ó��
    cv::Mat dilaImage;
    cv::dilate(threImage, dilaImage, cv::Mat(), cv::Point(-1, -1), 1);

    //�׵θ� ���� ó��
    cv::Mat cannImage;
    cv::Canny(threImage, cannImage, threshCanny, threshCanny * 2);

    //������ ������ Ž��
    vector<vector<cv::Point>> contourList;
    vector<cv::Vec4i> hierarchy;
    cv::findContours(cannImage, contourList, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    //�ٻ�ġ ����, ������ ó��
    for (int i = 0; i < contourList.size(); i++) {
        vector<cv::Point> approxList;
        cv::approxPolyDP(contourList[i], approxList, cv::arcLength(contourList[i], true) * 0.02, true);

        //�簢�� �ν�, �ν��� ������ �ִ�, �ּ� ���� ����
        if (approxList.size() == 4 && cv::isContourConvex(approxList) && cv::contourArea(approxList) > 1000 && cv::contourArea(approxList) < 919600) {

            //���簢������ ������ ���� ����
            double maxCosine = 0;
            for (int j = 2; j < 5; j++)
            {
                double cosine = GetCosineValue(approxList[j % 4], approxList[j - 2], approxList[j - 1]);
                maxCosine = MAX(maxCosine, cosine);
            }

            if (maxCosine < 0.5) {
                int min_x, max_x, min_y, max_y;

                max_x = std::max({ approxList[0].x, approxList[1].x, approxList[2].x, approxList[3].x });
                min_x = std::min({ approxList[0].x, approxList[1].x, approxList[2].x, approxList[3].x });
                max_y = std::max({ approxList[0].y, approxList[1].y, approxList[2].y, approxList[3].y });
                min_y = std::min({ approxList[0].y, approxList[1].y, approxList[2].y, approxList[3].y });

                //���ϴ�, ���� ��ǥ Ʃ���� ��ȯ�� ����Ʈ�� �߰�
                squareList.push_back(make_pair(cv::Point(min_x, min_y), cv::Point(max_x, max_y)));
            }
        }
    }
    return squareList;
}


/** @brief ĸó�� ī�޶� �̹����κ��� OCR �۾�, �������� �Ǻ�, ����ŷ �۾� ����.
*
*   @param[in]  api         tesseract OCR �۾� �ڵ鷯
*   @param[in]  opt         ����ŷ ó�� �ɼ�
*   @return     void
*/
void FilterCameraPI(tesseract::TessBaseAPI* api, int opt) {
    //���̽� ����� ������ ���� ���� ���� �����ð� ����
    Sleep(rand() % 100 + 1);

    //���簢�� �ν�, ���簢�� ����ŷ
    if (opt == 0) {
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

                //���簢�� �ν�
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                int x, y, w, h;
                for (int i = 0; i < squareList.size(); i++) {
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //���簢�� ����ŷ
                    cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                }

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
            semCapture.release();
        }
    }
    //���ڿ� ��ü �ν�, ���ڿ� ��ü ����ŷ
    if (opt == 1) {
        // ���� ��ü �߰� �� �ش� ��ü ����ŷ
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

                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                Boxa* boxes = api->GetComponentImages(tesseract::RIL_SYMBOL, true, NULL, NULL);
                for (int j = 0; j < boxes->n; j++) {
                    BOX* box = boxaGetBox(boxes, j, L_CLONE);
                    int x = box->x;
                    int y = box->y;
                    int w = box->w;
                    int h = box->h;

                    if (w * h < 280000) {
                        cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                    }

                    boxDestroy(&box);
                }
                /*
                * ���簢�� �ν� �ڵ� ��
                ***/
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

            semCapture.release();
        }
    }
    //���簢�� �ν�, ���簢�� �� �������� ���ڿ� ����ŷ
    if (opt == 2) {
        // ���簢�� �ν�, OCR ���� �� ����ŷ
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

                cv::Mat ocrImage;

                //���簢�� �ν�
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                //�̹��� ��ó��: ���ó��
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);

                //OCR �̹��� ����
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                for (int i = 0; i < squareList.size(); i++) {
                    int x, y, w, h;
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //OCR ������ ���簢�� ���� ����
                    api->SetRectangle(x, y, w, h);

                    //OCR ����
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
                                    cv::rectangle(originImage,
                                        cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                        cv::Scalar(0, 0, 0), -1);
                                }
                            }
                        } while (ri->Next(level));
                    }
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
    }
    //��� ���� �ν�, ��� ���� �� �������� ���ڿ� ����ŷ
    if (opt == 3) {
        while (true)
        {
            semInputQueue.acquire();
            //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
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
                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;


                /***
                * ��ü �ν� �ڵ� ����
                */
                cv::Mat gray_image;
                cv::cvtColor(originImage, gray_image, cv::COLOR_BGR2GRAY);

                ocrImage = gray_image;

                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                api->Recognize(0);

                tesseract::ResultIterator* ri = api->GetIterator();
                tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
                if (ri != 0) {
                    do {
                        float conf = ri->Confidence(level);
                        if (conf > 60) {
                            //const char* word = ri->GetUTF8Text(level);
                            string word = ri->GetUTF8Text(level);
                            int x1, y1, x2, y2;
                            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                            //cv::rectangle(origin_image_clone, cv::Rect(cv::Point(x1 * multiple_ratio, y1 * multiple_ratio), cv::Point(x2 * multiple_ratio, y2 * multiple_ratio)), cv::Scalar(0, 0, 0), -1);
                            cv::rectangle(originImage, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(0, 0, 0), -1);
                            //printf("word: '%s';  \tconf: %.2f;\n", word, conf);
                        }
                    } while (ri->Next(level));
                }
                /*
                * ��ü �ν� �ڵ� ��
                ***/

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

            semCapture.release();
        }
    }

    return;
}


/** @brief ĸó�� ī�޶� �̹����κ��� OCR �۾�, �������� �Ǻ�, ����ŷ �۾� ����, ���� ��� ����
*
*   @param[in]  api         tesseract OCR �۾� �ڵ鷯
*   @param[in]  opt         ����ŷ ó�� �ɼ�
*   @return     void
*/
void FilterCameraPI_BUFFER(tesseract::TessBaseAPI* api, int opt) {
    //���̽� ����� ������ ���� ���� ���� �����ð� ����
    Sleep(rand() % 100 + 1);

    //���簢�� �ν�, ���簢�� ����ŷ
    if (opt == 0) {
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

                //���簢�� �ν�
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                int x, y, w, h;
                for (int i = 0; i < squareList.size(); i++) {
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //���簢�� ����ŷ
                    cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                }

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    //���ڿ� ��ü �ν�, ���ڿ� ��ü ����ŷ
    if (opt == 1) {
        // ���� ��ü �߰� �� �ش� ��ü ����ŷ
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

                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                Boxa* boxes = api->GetComponentImages(tesseract::RIL_SYMBOL, true, NULL, NULL);
                for (int j = 0; j < boxes->n; j++) {
                    BOX* box = boxaGetBox(boxes, j, L_CLONE);
                    int x = box->x;
                    int y = box->y;
                    int w = box->w;
                    int h = box->h;

                    if (w * h < 280000) {
                        cv::rectangle(originImage, cv::Rect(cv::Point(x, y), cv::Point(x + w, y + h)), cv::Scalar(0, 0, 0), -1);
                    }

                    boxDestroy(&box);
                }
                /*
                * ���簢�� �ν� �ڵ� ��
                ***/
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }

        }
    }
    //���簢�� �ν�, ���簢�� �� �������� ���ڿ� ����ŷ
    if (opt == 2) {
        // ���簢�� �ν�, OCR ���� �� ����ŷ
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

                cv::Mat ocrImage;

                //���簢�� �ν�
                vector<pair<cv::Point, cv::Point>> squareList = GetSquareList(originImage);

                //�̹��� ��ó��: ���ó��
                cv::cvtColor(originImage, ocrImage, cv::COLOR_BGR2GRAY);

                //OCR �̹��� ����
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                for (int i = 0; i < squareList.size(); i++) {
                    int x, y, w, h;
                    x = squareList[i].first.x;
                    y = squareList[i].first.y;
                    w = squareList[i].second.x - x;
                    h = squareList[i].second.y - y;

                    //OCR ������ ���簢�� ���� ����
                    api->SetRectangle(x, y, w, h);

                    //OCR ����
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
                                    cv::rectangle(originImage,
                                        cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                        cv::Scalar(0, 0, 0), -1);
                                }
                            }
                        } while (ri->Next(level));
                    }
                }
                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    //��� ���� �ν�, ��� ���� �� �������� ���ڿ� ����ŷ
    if (opt == 3) {
        while (true)
        {
            semInputQueue.acquire();
            //printf("do_OCR Thread: %d\n", std::this_thread::get_id());
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
                cv::Mat ocrImage;
                double ratio = originImage.rows / (double)originImage.cols;


                /***
                * ��ü �ν� �ڵ� ����
                */
                cv::Mat gray_image;
                cv::cvtColor(originImage, gray_image, cv::COLOR_BGR2GRAY);
                cv::adaptiveThreshold(gray_image, ocrImage, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 51, 11);
                api->SetImage(ocrImage.data, ocrImage.cols, ocrImage.rows, 1, ocrImage.cols);
                api->Recognize(0);

                tesseract::ResultIterator* ri = api->GetIterator();
                tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
                if (ri != 0) {
                    do {
                        float conf = ri->Confidence(level);
                        if (conf > 50) {
                            string word = ri->GetUTF8Text(level);
                            if (CheckPI(word, _reList, _reStrList, _incList, _excList)) {
                                std::cout << word << std::endl;
                                int x1, y1, x2, y2;
                                ri->BoundingBox(level, &x1, &y1, &x2, &y2);
                                cv::rectangle(originImage,
                                    cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                    cv::Scalar(0, 0, 0), -1);
                                /*
                                cv::rectangle(ocrImage,
                                    cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)),
                                    cv::Scalar(0, 0, 0), -1);
                                */

                            }
                        }
                    } while (ri->Next(level));
                }
                /*
                * ��ü �ν� �ڵ� ��
                ***/

                semOutputQueue.acquire();
                outputQueue.push(make_pair(frameCounter, originImage));
                /*
                outputQueue.push(make_pair(frameCounter, ocrImage));
                */
                semOutputQueue.release();
            }
            else {
                semInputQueue.release();
            }
        }
    }
    return;
}


bool FilterCameraPI_DEBUG(tesseract::TessBaseAPI* api) {
    while (true)
    {
        semInputQueue.acquire();
        //printf("do_OCR_fake Thread: %d\n", std::this_thread::get_id());
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

            vector<pair<cv::Point, cv::Point>> rectangle_points = GetSquareList(origin_image_clone);

            semOutputQueue.acquire();
            outputQueue.push(make_pair(frame_counter, origin_image_clone));
            semOutputQueue.release();
        }
        else {
            semInputQueue.release();
        }
        semCapture.release();
    }
    return true;
}


/** @brief ���� ī�޶�� ����ŷ ó���� �̹��� ���.
*
*   @param[in]    vCam      tesseract OCR �۾� �ڵ鷯
*   @return       void
*/
void ExportVirtualCam(VirtualOutput* vCam) {
    while (true)
    {
        semOutputQueue.acquire();
        if (!outputQueue.empty()) {
            if (outputQueue.top().first == currentFrameCounter) {
                cv::Mat image = outputQueue.top().second;
                outputQueue.pop();
                semOutputQueue.release();

                //���� ī�޶�� �̹��� ���
                vCam->send(image.data);
                currentFrameCounter++;
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


/** @brief ���� ī�޶�� ����ŷ ó���� �̹��� ���, ���� ��� ����
*
*   @param[in]    vCam      tesseract OCR �۾� �ڵ鷯
*   @return       void
*/
void ExportVirtualCam_BUFFER(VirtualOutput* vCam, int fps, int bufferSize) {
    bool queueSizeFlag = false;
    while (true)
    {
        printf("outputQueue.size()=%zd\n", outputQueue.size());
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

                //���� ī�޶�� �̹��� ���
                vCam->send(image.data);

                //�ʴ� ������ ����
                Sleep(1000 / fps);
                currentFrameCounter++;
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



/** @brief �־��� ī�޶󿡼� �̹��� ĸó.
*
*   @param[in]    fCounter  ������� ĸó�� �̹��� ����
*   @param[in]    width     ĸó ī�޶� ���, �ʺ�
*   @param[in]    height    ĸó ī�޶� ���, ����
*   @param[in]    fps       ĸó ī�޶� ���, �ʴ� ĸó ������
*   @param[in]    camNumber ĸó�� ī�޶� ��ȣ
*   @return       bool      ���� �߻� �� false ��ȯ
*/
bool CaptureCamera(int* fCounter, int width, int height, int fps, int camNumber) {
    //ī�޶� �۵� Ȯ��
    cv::VideoCapture capture(camNumber, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        cout << "���õ� ī�޶� ����� �� �����ϴ�." << endl;
        return false;
    }

    //ī�޶� ��� ����
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FPS, fps);

    while (true)
    {
        //����� �޸� �ִ� �뷮 ����
        //!TODO: �����ص� �� ��� ������? ���� �ʿ�
        if (inputQueue.size() > 512) {
            continue;
        }

        //�̹��� ĸó
        cv::Mat image;
        image.create(height, width, CV_8UC3);
        if (!capture.grab()) {
            cout << "Failed to grab frame!" << endl;
            continue;
        }
        if (!capture.retrieve(image)) {
            cout << "Failed to retrieve frame!" << endl;
            continue;
        }

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, image));
        (*fCounter)++;
        semInputQueue.release();

        //�޸� �뷮 ���� ����
        //�������� ó�� �����尡 ���� ��, semCapture�� ������
        semCapture.acquire();
    }
    return true;
}


/** @brief �־��� ī�޶󿡼� �̹��� ĸó, ���� ��� ����
*
*   @param[in]    fCounter  ������� ĸó�� �̹��� ����
*   @param[in]    width     ĸó ī�޶� ���, �ʺ�
*   @param[in]    height    ĸó ī�޶� ���, ����
*   @param[in]    fps       ĸó ī�޶� ���, �ʴ� ĸó ������
*   @param[in]    camNumber ĸó�� ī�޶� ��ȣ
*   @return       bool      ���� �߻� �� false ��ȯ
*/
bool CaptureCamera_VIDEO(int* fCounter, int width, int height, int fps, int camNumber) {
    //ī�޶� �۵� Ȯ��
    cv::VideoCapture capture(camNumber, cv::CAP_DSHOW);
    if (!capture.isOpened()) {
        cout << "���õ� ī�޶� ����� �� �����ϴ�." << endl;
        return false;
    }

    //ī�޶� ��� ����
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FPS, fps);

    while (true)
    {
        //�̹��� ĸó
        cv::Mat image;
        image.create(height, width, CV_8UC3);
        if (!capture.grab()) {
            cout << "Failed to grab frame!" << endl;
            continue;
        }
        if (!capture.retrieve(image)) {
            cout << "Failed to retrieve frame!" << endl;
            continue;
        }

        semInputQueue.acquire();
        inputQueue.push(make_pair(*fCounter, image));
        (*fCounter)++;
        semInputQueue.release();


        Sleep(1000 / fps);
    }
    return true;
}


int main(int argc, char* argv[])
{
    // C:\Users\Eungyu\source\repos\miniprj-masking-pi\external\share\tessdata
    if (!argv[1]) {
        std::cout << "�����ͼ� ������ �����ϴ� ��θ� ù��° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }
    const char* TESSDATA_PATH = argv[1];


    if (!argv[2]) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum;

            hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
            if (SUCCEEDED(hr))
            {
                DisplayDeviceInformation(pEnum);
                pEnum->Release();
            }
            CoUninitialize();
        }
        return 1;
    }


    char* p;
    long cam = strtol(argv[2], &p, 10);
    if (*p != '\0') {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr))
        {
            IEnumMoniker* pEnum;

            hr = EnumerateDevices(CLSID_VideoInputDeviceCategory, &pEnum);
            if (SUCCEEDED(hr))
            {
                DisplayDeviceInformation(pEnum);
                pEnum->Release();
            }
            CoUninitialize();
        }
        return 1;
    }

    if (!argv[3]) {
        std::cout << "�������� ���͸� ��� ��ȣ�� ����° ���ڿ� �Է����ּ���." << std::endl;
        std::cout << "[0]: ���簢�� ���͸�" << std::endl;
        std::cout << "[1]: ���� ��ü ���͸�" << std::endl;
        std::cout << "[2]: ���簢���� �����Ͽ� �������� ���͸�" << std::endl;
        std::cout << "[3]: ��� ������ ���Ͽ� �������� ���͸�" << std::endl;

        return 1;
    }

    char* q;
    long mask_opiton = strtol(argv[3], &q, 10);
    if (*q != '\0') {
        std::cout << "�������� ���͸� ��� ��ȣ�� ����° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }

    if (!argv[4]) {
        std::cout << "����� ���� ������ ������ �׹�° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }

    char* r;
    long _bufferSize = strtol(argv[4], &r, 10);
    if (*r != '\0') {
        std::cout << "����� ���� ������ ������ �׹�° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }


    if (!argv[5]) {
        std::cout << "����� �����ͺ��̽� ��θ� �ټ���° ���ڿ� �Է����ּ���." << std::endl;
        return 1;
    }

    SQLite::Database db(argv[5], SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    InitPICheckerRegex(&db, _reList, _reStrList);
    InitPICheckerUser(&db, _incList, _excList);


    int width = 1280;
    int height = 720;
    int fps = 15;
    int bufferSize = (int)_bufferSize;
    int fourcc = cv::VideoWriter::fourcc('2', '4', 'B', 'G');
    VirtualOutput* vo = new VirtualOutput(width, height, fps, fourcc, "Unity Video Capture");
    std::cout << "���� ī�޶� " << vo->device() << "�� �̿��Ͽ� ����մϴ�." << std::endl;

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

    int frame_counter = 1;

    //ĸó ������ ����, �񵿱�
    /*
    std::future<void> threadCapture = std::async(
        std::launch::async,
        [&frame_counter, width, height, fps, cam]() {
            CaptureCamera(&frame_counter, width, height, fps, cam);
        });
    */

    //�������� ó�� ������ ����, �񵿱�
    //!TODO: [������ CPU�� ������ ���� - 1]��ŭ ������ ����
    /*
    auto ocr1 = std::async(std::launch::async, [api1, mask_opiton]() { FilterCameraPI(api1, mask_opiton); });
    auto ocr2 = std::async(std::launch::async, [api2, mask_opiton]() { FilterCameraPI(api2, mask_opiton); });
    auto ocr3 = std::async(std::launch::async, [api3, mask_opiton]() { FilterCameraPI(api3, mask_opiton); });
    auto ocr4 = std::async(std::launch::async, [api4, mask_opiton]() { FilterCameraPI(api4, mask_opiton); });
    auto ocr5 = std::async(std::launch::async, [api5, mask_opiton]() { FilterCameraPI(api5, mask_opiton); });
    auto ocr6 = std::async(std::launch::async, [api6, mask_opiton]() { FilterCameraPI(api6, mask_opiton); });
    auto ocr7 = std::async(std::launch::async, [api7, mask_opiton]() { FilterCameraPI(api7, mask_opiton); });
    auto ocr8 = std::async(std::launch::async, [api8, mask_opiton]() { FilterCameraPI(api8, mask_opiton); });
    */


    //���� ī�޶� ��� ������ ����, �񵿱�
    /*
    std::future<void> threadExport = std::async(
        std::launch::async, [vo]() {
            ExportVirtualCam(vo);
        });
    */

    std::future<void> threadCapture = std::async(
        std::launch::async,
        [&frame_counter, width, height, fps, cam]() {
            CaptureCamera_VIDEO(&frame_counter, width, height, fps, cam);
        });
    auto ocr1 = std::async(std::launch::async, [api1, mask_opiton]() { FilterCameraPI_BUFFER(api1, mask_opiton); });
    auto ocr2 = std::async(std::launch::async, [api2, mask_opiton]() { FilterCameraPI_BUFFER(api2, mask_opiton); });
    auto ocr3 = std::async(std::launch::async, [api3, mask_opiton]() { FilterCameraPI_BUFFER(api3, mask_opiton); });
    auto ocr4 = std::async(std::launch::async, [api4, mask_opiton]() { FilterCameraPI_BUFFER(api4, mask_opiton); });
    auto ocr5 = std::async(std::launch::async, [api5, mask_opiton]() { FilterCameraPI_BUFFER(api5, mask_opiton); });
    auto ocr6 = std::async(std::launch::async, [api6, mask_opiton]() { FilterCameraPI_BUFFER(api6, mask_opiton); });
    auto ocr7 = std::async(std::launch::async, [api7, mask_opiton]() { FilterCameraPI_BUFFER(api7, mask_opiton); });
    auto ocr8 = std::async(std::launch::async, [api8, mask_opiton]() { FilterCameraPI_BUFFER(api8, mask_opiton); });
    auto ocr9 = std::async(std::launch::async, [api9, mask_opiton]() { FilterCameraPI_BUFFER(api9, mask_opiton); });
    auto ocr10 = std::async(std::launch::async, [api10, mask_opiton]() { FilterCameraPI_BUFFER(api10, mask_opiton); });
    auto ocr11 = std::async(std::launch::async, [api11, mask_opiton]() { FilterCameraPI_BUFFER(api11, mask_opiton); });
    auto ocr12 = std::async(std::launch::async, [api12, mask_opiton]() { FilterCameraPI_BUFFER(api12, mask_opiton); });
    auto ocr13 = std::async(std::launch::async, [api13, mask_opiton]() { FilterCameraPI_BUFFER(api13, mask_opiton); });
    auto ocr14 = std::async(std::launch::async, [api14, mask_opiton]() { FilterCameraPI_BUFFER(api14, mask_opiton); });
    auto ocr15 = std::async(std::launch::async, [api15, mask_opiton]() { FilterCameraPI_BUFFER(api15, mask_opiton); });
    std::future<void> threadExport = std::async(
        std::launch::async, [vo, fps, bufferSize]() {
            ExportVirtualCam_BUFFER(vo, fps, bufferSize);
        });

    _CrtDumpMemoryLeaks();
    return 0;
}