# [MOSAIC](https://softcon.ajou.ac.kr/works/works.asp?uid=599)
<img width="776" alt="KakaoTalk_20220605_182616249" src="https://user-images.githubusercontent.com/106902217/172044737-2fb55b4f-9f7d-42fd-bb8d-1d0b4276220a.png">

## About
[MOSAIC](https://softcon.ajou.ac.kr/works/works.asp?uid=599)는 데스크탑 앱을 활용한 민감 개인정보 관리의 약자로, 타인과 디지털 데이터를 공유하는 다양한 상황 속에서 의도치 않게 발생하는 사생활 노출을 방지하기 위한 서비스입니다.

카메라 필터링, 화면 필터링, 업로드 보안(문서 보안, 메타데이터 소거)와 같은 기능들을 제공하여 민감한 정보를 실시간 비식별화합니다. 

- 카메라 필터링: 실시간으로 PC에 장착된 카메라로부터 동영상 스트림을 받아 OCR을 통해 텍스트를 얻어낸 후, 개인정보를 식별하여 마스킹 처리, 최종적으로 가상 카메라로 송출합니다.


- 화면 필터링: 카메라 필터링과 유사하게 PC의 디스플레이로부터 받은 동영상 스트림에서 개인정보를 식별, 마스킹 처리하여 최종적으로 가상 디스플레이로 송출합니다.


- 업로드 보안: 업로드 이벤트를 탐지하여 중간에 워드, 한글 등의 문서 파일, 사진, 동영상 등의 미디어 파일을 가로채 개인정보를 식별, 마스킹 처리하거나 메타데이터를 소거하여 업로드를 대신 수행합니다.

아주대학교 2022-1 SOFTCON 캡스톤 디자인 부문에서 우수상을 수상하였습니다. [링크](https://softcon.ajou.ac.kr/community/notice_view.asp?page=1&sword=&uid=49)



## Structure
```bash
MOSAIC
├─external
│  ├─UnityCapture-master	가상 카메라 설치 파일
│  └─usbmmidd_v2		가상 디스플레이 설치 파일
├─masking-camera		카메라 필터링 기능
├─masking-screen		화면 필터링 기능
├─resource			이미지 저장용 디렉터리
├─tools
│  └─tiny-db-manager		GUI의 DB 관련 기능
├─upload-security		업로드 보안 기능(메타데이터 소거, 문서 보안, 업로드 탐지)
└─winform-gui			GUI
```


## Reference
- OCR
  - tesseract: https://github.com/tesseract-ocr/tesseract
- Database (tiny-db-manager)
  - SQLiteCpp: https://github.com/SRombauts/SQLiteCpp
- Virtual Camera (masking-camera)
  - pyvirtualcam: https://github.com/letmaik/pyvirtualcam
  - UnityCapture: https://github.com/schellingb/UnityCapture
- Virtual Display (masking-screen)
  - Amyuni USB Mobile Monitor: https://www.amyuni.com/forum/viewtopic.php?t=3030


## Screenshots
- GUI
<img src="https://user-images.githubusercontent.com/106902217/172041730-7311384d-97b0-4eed-84ad-76bf6fc183f2.png" width="70%"/>



- 카메라 필터링
<img src="https://user-images.githubusercontent.com/106902217/172045178-23129056-bb2d-46c2-a3bc-4312f4eeb9d4.png" width="70%"/>



- 화면 필터링
<img src="https://user-images.githubusercontent.com/106902217/172045181-114b56c5-1324-4c1b-b5a5-20dfbd8bc4e2.png" width="70%"/>



- 업로드 보안 中 문서 보안
<img src="https://user-images.githubusercontent.com/106902217/172045162-5128e1c5-155e-4d5b-b30e-f9d25be88e61.png" width="70%"/>



- 업로드 보안 中 메타데이터 보안
<img src="https://user-images.githubusercontent.com/106902217/172045172-fa9ee611-b9f6-4fe7-9495-9e3f9f2a1d5d.png" width="70%"/>





## Developers
아주대학교 사이버보안학과 소속

- [김소정](https://github.com/ssoL2): 업로드 보안 中 메타데이터 소거 기능, 데이터베이스 담당    

- [김만준](https://github.com/MANJUNKIM): 업로드 보안 中 문서 보안 기능, 업로드 탐지 기능 담당

- [박은규](https://github.com/Eungyu-dev): 카메라 필터링 기능, 화면 필터링 기능, GUI 담당

- [박현민](https://github.com/qkrgusals98): 개인정보판별 알고리즘, 데이터베이스 담당 
