#include "fh3_text.h"
#include "shared/support.h"

/* FH3's string tables (media\stripped\stringtables) are EN, GB, JP, DE, FR,
   MX, ES, IT, KO, CHS, CHT, CZ, HU, PL, RU, NL and BR. English (EN, GB) uses
   the English text; the others have a column each. */
enum { DE, FR, IT, ES, MX, BR, NL, PL, CZ, HU, RU, JP, KO, CHS, CHT, LANGUAGES, EN = LANGUAGES };
static const char *const codes[] = {"DE", "FR", "IT", "ES", "MX", "BR", "NL", "PL",
                                    "CZ", "HU", "RU", "JP", "KO", "CHS", "CHT", "EN"};

typedef struct {
    const char *english;
    const char *text[LANGUAGES];
} Text;

static const Text texts[] = {
    /* ANNA */
    {"Car", {"Auto", "Voiture", "Auto", "Coche", "Auto", "Carro", "Auto", "Samochód", "Auto", "Autó",
             "Машина", "車", "차량", "车辆", "車輛"}},
    {"Lights", {"Lichter", "Feux", "Luci", "Luces", "Luces", "Luzes", "Lichten", "Światła", "Světla",
                "Lámpák", "Фары", "ライト", "조명", "灯光", "燈光"}},
    {"Doors", {"Türen", "Portières", "Portiere", "Puertas", "Puertas", "Portas", "Deuren", "Drzwi",
               "Dveře", "Ajtók", "Двери", "ドア", "도어", "车门", "車門"}},
    {"Autodrive", {"Autopilot", "Conduite auto", "Guida automatica", "Conducción automática",
                   "Conducción automática", "Direção automática", "Automatisch rijden", "Autopilot",
                   "Autopilot", "Önvezetés", "Автопилот", "自動運転", "자동 주행", "自动驾驶",
                   "自動駕駛"}},
    {"Open Roof", {"Dach öffnen", "Ouvrir le toit", "Apri il tetto", "Abrir techo", "Abrir techo",
                   "Abrir teto", "Dak openen", "Otwórz dach", "Otevřít střechu", "Tető nyitása",
                   "Открыть крышу", "ルーフを開ける", "루프 열기", "打开车顶", "打開車頂"}},
    {"Close Roof", {"Dach schließen", "Fermer le toit", "Chiudi il tetto", "Cerrar techo",
                    "Cerrar techo", "Fechar teto", "Dak sluiten", "Zamknij dach", "Zavřít střechu",
                    "Tető zárása", "Закрыть крышу", "ルーフを閉める", "루프 닫기", "关闭车顶",
                    "關閉車頂"}},
    {"Open Hood", {"Motorhaube öffnen", "Ouvrir le capot", "Apri il cofano", "Abrir capó",
                   "Abrir cofre", "Abrir capô", "Motorkap openen", "Otwórz maskę", "Otevřít kapotu",
                   "Motorháztető nyitása", "Открыть капот", "ボンネットを開ける", "보닛 열기",
                   "打开引擎盖", "打開引擎蓋"}},
    {"Close Hood", {"Motorhaube schließen", "Fermer le capot", "Chiudi il cofano", "Cerrar capó",
                    "Cerrar cofre", "Fechar capô", "Motorkap sluiten", "Zamknij maskę",
                    "Zavřít kapotu", "Motorháztető zárása", "Закрыть капот", "ボンネットを閉める",
                    "보닛 닫기", "关闭引擎盖", "關閉引擎蓋"}},
    {"Open Trunk", {"Kofferraum öffnen", "Ouvrir le coffre", "Apri il bagagliaio",
                    "Abrir maletero", "Abrir cajuela", "Abrir porta-malas", "Kofferbak openen",
                    "Otwórz bagażnik", "Otevřít kufr", "Csomagtartó nyitása", "Открыть багажник",
                    "トランクを開ける", "트렁크 열기", "打开后备箱", "打開後車廂"}},
    {"Close Trunk", {"Kofferraum schließen", "Fermer le coffre", "Chiudi il bagagliaio",
                     "Cerrar maletero", "Cerrar cajuela", "Fechar porta-malas", "Kofferbak sluiten",
                     "Zamknij bagażnik", "Zavřít kufr", "Csomagtartó zárása", "Закрыть багажник",
                     "トランクを閉める", "트렁크 닫기", "关闭后备箱", "關閉後車廂"}},
    {"Open Front Left Door",
     {"Vordere linke Tür öffnen", "Ouvrir la portière avant gauche",
      "Apri la portiera anteriore sinistra", "Abrir puerta delantera izquierda",
      "Abrir puerta delantera izquierda", "Abrir porta dianteira esquerda", "Linkervoordeur openen",
      "Otwórz lewe przednie drzwi", "Otevřít levé přední dveře", "Bal első ajtó nyitása",
      "Открыть левую переднюю дверь", "左フロントドアを開ける", "왼쪽 앞문 열기", "打开左前门",
      "打開左前門"}},
    {"Close Front Left Door",
     {"Vordere linke Tür schließen", "Fermer la portière avant gauche",
      "Chiudi la portiera anteriore sinistra", "Cerrar puerta delantera izquierda",
      "Cerrar puerta delantera izquierda", "Fechar porta dianteira esquerda",
      "Linkervoordeur sluiten", "Zamknij lewe przednie drzwi", "Zavřít levé přední dveře",
      "Bal első ajtó zárása", "Закрыть левую переднюю дверь", "左フロントドアを閉める",
      "왼쪽 앞문 닫기", "关闭左前门", "關閉左前門"}},
    {"Open Front Right Door",
     {"Vordere rechte Tür öffnen", "Ouvrir la portière avant droite",
      "Apri la portiera anteriore destra", "Abrir puerta delantera derecha",
      "Abrir puerta delantera derecha", "Abrir porta dianteira direita", "Rechtervoordeur openen",
      "Otwórz prawe przednie drzwi", "Otevřít pravé přední dveře", "Jobb első ajtó nyitása",
      "Открыть правую переднюю дверь", "右フロントドアを開ける", "오른쪽 앞문 열기", "打开右前门",
      "打開右前門"}},
    {"Close Front Right Door",
     {"Vordere rechte Tür schließen", "Fermer la portière avant droite",
      "Chiudi la portiera anteriore destra", "Cerrar puerta delantera derecha",
      "Cerrar puerta delantera derecha", "Fechar porta dianteira direita",
      "Rechtervoordeur sluiten", "Zamknij prawe przednie drzwi", "Zavřít pravé přední dveře",
      "Jobb első ajtó zárása", "Закрыть правую переднюю дверь", "右フロントドアを閉める",
      "오른쪽 앞문 닫기", "关闭右前门", "關閉右前門"}},
    {"Open Rear Left Door",
     {"Hintere linke Tür öffnen", "Ouvrir la portière arrière gauche",
      "Apri la portiera posteriore sinistra", "Abrir puerta trasera izquierda",
      "Abrir puerta trasera izquierda", "Abrir porta traseira esquerda", "Linkerachterdeur openen",
      "Otwórz lewe tylne drzwi", "Otevřít levé zadní dveře", "Bal hátsó ajtó nyitása",
      "Открыть левую заднюю дверь", "左リアドアを開ける", "왼쪽 뒷문 열기", "打开左后门",
      "打開左後門"}},
    {"Close Rear Left Door",
     {"Hintere linke Tür schließen", "Fermer la portière arrière gauche",
      "Chiudi la portiera posteriore sinistra", "Cerrar puerta trasera izquierda",
      "Cerrar puerta trasera izquierda", "Fechar porta traseira esquerda",
      "Linkerachterdeur sluiten", "Zamknij lewe tylne drzwi", "Zavřít levé zadní dveře",
      "Bal hátsó ajtó zárása", "Закрыть левую заднюю дверь", "左リアドアを閉める",
      "왼쪽 뒷문 닫기", "关闭左后门", "關閉左後門"}},
    {"Open Rear Right Door",
     {"Hintere rechte Tür öffnen", "Ouvrir la portière arrière droite",
      "Apri la portiera posteriore destra", "Abrir puerta trasera derecha",
      "Abrir puerta trasera derecha", "Abrir porta traseira direita", "Rechterachterdeur openen",
      "Otwórz prawe tylne drzwi", "Otevřít pravé zadní dveře", "Jobb hátsó ajtó nyitása",
      "Открыть правую заднюю дверь", "右リアドアを開ける", "오른쪽 뒷문 열기", "打开右后门",
      "打開右後門"}},
    {"Close Rear Right Door",
     {"Hintere rechte Tür schließen", "Fermer la portière arrière droite",
      "Chiudi la portiera posteriore destra", "Cerrar puerta trasera derecha",
      "Cerrar puerta trasera derecha", "Fechar porta traseira direita",
      "Rechterachterdeur sluiten", "Zamknij prawe tylne drzwi", "Zavřít pravé zadní dveře",
      "Jobb hátsó ajtó zárása", "Закрыть правую заднюю дверь", "右リアドアを閉める",
      "오른쪽 뒷문 닫기", "关闭右后门", "關閉右後門"}},
    {"Open All", {"Alle öffnen", "Tout ouvrir", "Apri tutto", "Abrir todo", "Abrir todo",
                  "Abrir tudo", "Alles openen", "Otwórz wszystko", "Otevřít vše", "Összes nyitása",
                  "Открыть всё", "すべて開ける", "모두 열기", "全部打开", "全部打開"}},
    {"Close All", {"Alle schließen", "Tout fermer", "Chiudi tutto", "Cerrar todo", "Cerrar todo",
                   "Fechar tudo", "Alles sluiten", "Zamknij wszystko", "Zavřít vše",
                   "Összes zárása", "Закрыть всё", "すべて閉める", "모두 닫기", "全部关闭",
                   "全部關閉"}},
    {"Headlights On", {"Scheinwerfer an", "Allumer les phares", "Accendi i fari", "Encender faros",
                       "Encender faros", "Ligar faróis", "Koplampen aan", "Włącz reflektory",
                       "Zapnout světlomety", "Fényszórók be", "Включить фары",
                       "ヘッドライトをオン", "헤드라이트 켜기", "打开前大灯", "打開頭燈"}},
    {"Headlights Off", {"Scheinwerfer aus", "Éteindre les phares", "Spegni i fari",
                        "Apagar faros", "Apagar faros", "Desligar faróis", "Koplampen uit",
                        "Wyłącz reflektory", "Vypnout světlomety", "Fényszórók ki",
                        "Выключить фары", "ヘッドライトをオフ", "헤드라이트 끄기", "关闭前大灯",
                        "關閉頭燈"}},
    {"Hazard Lights On",
     {"Warnblinker an", "Allumer les feux de détresse", "Accendi le quattro frecce",
      "Encender luces de emergencia", "Encender luces intermitentes", "Ligar pisca-alerta",
      "Alarmlichten aan", "Włącz światła awaryjne", "Zapnout výstražná světla", "Vészvillogó be",
      "Включить аварийные огни", "ハザードランプをオン", "비상등 켜기", "打开双闪", "打開警示燈"}},
    {"Hazard Lights Off",
     {"Warnblinker aus", "Éteindre les feux de détresse", "Spegni le quattro frecce",
      "Apagar luces de emergencia", "Apagar luces intermitentes", "Desligar pisca-alerta",
      "Alarmlichten uit", "Wyłącz światła awaryjne", "Vypnout výstražná světla", "Vészvillogó ki",
      "Выключить аварийные огни", "ハザードランプをオフ", "비상등 끄기", "关闭双闪",
      "關閉警示燈"}},
    {"Left Indicator On",
     {"Blinker links an", "Allumer le clignotant gauche", "Accendi la freccia sinistra",
      "Encender intermitente izquierdo", "Encender direccional izquierda", "Ligar seta esquerda",
      "Richtingaanwijzer links aan", "Włącz lewy kierunkowskaz", "Zapnout levý blinkr",
      "Bal index be", "Включить левый поворотник", "左ウインカーをオン", "왼쪽 방향지시등 켜기",
      "打开左转向灯", "打開左方向燈"}},
    {"Left Indicator Off",
     {"Blinker links aus", "Éteindre le clignotant gauche", "Spegni la freccia sinistra",
      "Apagar intermitente izquierdo", "Apagar direccional izquierda", "Desligar seta esquerda",
      "Richtingaanwijzer links uit", "Wyłącz lewy kierunkowskaz", "Vypnout levý blinkr",
      "Bal index ki", "Выключить левый поворотник", "左ウインカーをオフ", "왼쪽 방향지시등 끄기",
      "关闭左转向灯", "關閉左方向燈"}},
    {"Right Indicator On",
     {"Blinker rechts an", "Allumer le clignotant droit", "Accendi la freccia destra",
      "Encender intermitente derecho", "Encender direccional derecha", "Ligar seta direita",
      "Richtingaanwijzer rechts aan", "Włącz prawy kierunkowskaz", "Zapnout pravý blinkr",
      "Jobb index be", "Включить правый поворотник", "右ウインカーをオン",
      "오른쪽 방향지시등 켜기", "打开右转向灯", "打開右方向燈"}},
    {"Right Indicator Off",
     {"Blinker rechts aus", "Éteindre le clignotant droit", "Spegni la freccia destra",
      "Apagar intermitente derecho", "Apagar direccional derecha", "Desligar seta direita",
      "Richtingaanwijzer rechts uit", "Wyłącz prawy kierunkowskaz", "Vypnout pravý blinkr",
      "Jobb index ki", "Выключить правый поворотник", "右ウインカーをオフ",
      "오른쪽 방향지시등 끄기", "关闭右转向灯", "關閉右方向燈"}},
    /* Menus */
    {"Jump to Manufacturer",
     {"Zum Hersteller springen", "Aller au constructeur", "Vai al costruttore",
      "Ir al fabricante", "Ir al fabricante", "Ir para o fabricante", "Naar fabrikant",
      "Przejdź do producenta", "Přejít na výrobce", "Ugrás a gyártóhoz", "Перейти к производителю",
      "メーカーへジャンプ", "제조사로 이동", "跳转至制造商", "跳至製造商"}},
    {"Return to Garage",
     {"Zurück zur Garage", "Retour au garage", "Torna al garage", "Volver al garaje",
      "Regresar al garaje", "Voltar à garagem", "Terug naar garage", "Powrót do garażu",
      "Zpět do garáže", "Vissza a garázsba", "Вернуться в гараж", "ガレージに戻る",
      "차고로 돌아가기", "返回车库", "返回車庫"}},
    {"Manufacturer", {"Hersteller", "Constructeur", "Costruttore", "Fabricante", "Fabricante",
                      "Fabricante", "Fabrikant", "Producent", "Výrobce", "Gyártó", "Производитель",
                      "メーカー", "제조사", "制造商", "製造商"}},
    /* Settings > HUD and Gameplay, Video */
    {"Chase Near FOV",
     {"Sichtfeld: Verfolger nah", "Champ de vision : poursuite proche",
      "Campo visivo: inseguimento vicino", "Campo de visión: persecución cercana",
      "Campo de visión: persecución cercana", "Campo de visão: perseguição próxima",
      "Gezichtsveld: achtervolging dichtbij", "Pole widzenia: pościg blisko",
      "Zorné pole: pronásledování zblízka", "Látószög: üldöző közeli",
      "Обзор: ближняя камера погони", "視野角：追従（近）", "시야각: 추적 근거리",
      "视野：近距离追尾", "視野：近距離追尾"}},
    {"Chase Far FOV",
     {"Sichtfeld: Verfolger fern", "Champ de vision : poursuite lointaine",
      "Campo visivo: inseguimento lontano", "Campo de visión: persecución lejana",
      "Campo de visión: persecución lejana", "Campo de visão: perseguição distante",
      "Gezichtsveld: achtervolging veraf", "Pole widzenia: pościg daleko",
      "Zorné pole: pronásledování zdálky", "Látószög: üldöző távoli",
      "Обзор: дальняя камера погони", "視野角：追従（遠）", "시야각: 추적 원거리",
      "视野：远距离追尾", "視野：遠距離追尾"}},
    {"Hood FOV", {"Sichtfeld: Motorhaube", "Champ de vision : capot", "Campo visivo: cofano",
                  "Campo de visión: capó", "Campo de visión: cofre", "Campo de visão: capô",
                  "Gezichtsveld: motorkap", "Pole widzenia: maska", "Zorné pole: kapota",
                  "Látószög: motorháztető", "Обзор: капот", "視野角：ボンネット", "시야각: 보닛",
                  "视野：引擎盖", "視野：引擎蓋"}},
    {"Bumper FOV", {"Sichtfeld: Stoßstange", "Champ de vision : pare-chocs",
                    "Campo visivo: paraurti", "Campo de visión: parachoques",
                    "Campo de visión: defensa", "Campo de visão: para-choque",
                    "Gezichtsveld: bumper", "Pole widzenia: zderzak", "Zorné pole: nárazník",
                    "Látószög: lökhárító", "Обзор: бампер", "視野角：バンパー", "시야각: 범퍼",
                    "视野：保险杠", "視野：保險桿"}},
    {"Cockpit FOV", {"Sichtfeld: Cockpit", "Champ de vision : cockpit", "Campo visivo: abitacolo",
                     "Campo de visión: cabina", "Campo de visión: cabina", "Campo de visão: cockpit",
                     "Gezichtsveld: cockpit", "Pole widzenia: kokpit", "Zorné pole: kokpit",
                     "Látószög: pilótafülke", "Обзор: кокпит", "視野角：コックピット",
                     "시야각: 콕핏", "视野：驾驶舱", "視野：駕駛艙"}},
    {"Minimap Follows Camera",
     {"Minikarte folgt Kamera", "Mini-carte suit la caméra", "La minimappa segue la telecamera",
      "El minimapa sigue a la cámara", "El minimapa sigue a la cámara",
      "O minimapa segue a câmera", "Minikaart volgt camera", "Minimapa podąża za kamerą",
      "Minimapa sleduje kameru", "A minitérkép követi a kamerát", "Миникарта следует за камерой",
      "ミニマップをカメラに追従", "미니맵이 카메라를 따라감", "小地图跟随镜头", "小地圖跟隨鏡頭"}},
    {"Autodrive Cinematic Camera",
     {"Autopilot-Kinokamera", "Caméra cinématique de conduite auto",
      "Telecamera cinematica guida automatica", "Cámara cinematográfica de conducción automática",
      "Cámara cinematográfica de conducción automática", "Câmera cinematográfica da direção automática",
      "Filmcamera automatisch rijden", "Kamera filmowa autopilota", "Filmová kamera autopilota",
      "Önvezetés filmes kamera", "Кинокамера автопилота", "自動運転シネマカメラ",
      "자동 주행 시네마틱 카메라", "自动驾驶电影镜头", "自動駕駛電影鏡頭"}},
    {"Show FPS", {"FPS anzeigen", "Afficher les FPS", "Mostra FPS", "Mostrar FPS", "Mostrar FPS",
                  "Mostrar FPS", "FPS tonen", "Pokaż FPS", "Zobrazit FPS", "FPS megjelenítése",
                  "Показывать FPS", "FPSを表示", "FPS 표시", "显示 FPS", "顯示 FPS"}},
    {"Show GPU Usage",
     {"GPU-Auslastung anzeigen", "Afficher l'utilisation du GPU", "Mostra utilizzo GPU",
      "Mostrar uso de GPU", "Mostrar uso de GPU", "Mostrar uso da GPU", "GPU-gebruik tonen",
      "Pokaż użycie GPU", "Zobrazit využití GPU", "GPU-használat megjelenítése",
      "Показывать загрузку GPU", "GPU使用率を表示", "GPU 사용률 표시", "显示 GPU 占用",
      "顯示 GPU 使用率"}},
    {"Show Frame Time",
     {"Bildzeit anzeigen", "Afficher le temps d'image", "Mostra tempo fotogramma",
      "Mostrar tiempo de fotograma", "Mostrar tiempo de cuadro", "Mostrar tempo de quadro",
      "Frametijd tonen", "Pokaż czas klatki", "Zobrazit čas snímku", "Képkocka-idő megjelenítése",
      "Показывать время кадра", "フレームタイムを表示", "프레임 시간 표시", "显示帧时间",
      "顯示影格時間"}},
    {"Show FPS Background",
     {"FPS-Hintergrund anzeigen", "Afficher le fond des FPS", "Mostra sfondo FPS",
      "Mostrar fondo de FPS", "Mostrar fondo de FPS", "Mostrar fundo do FPS",
      "FPS-achtergrond tonen", "Pokaż tło FPS", "Zobrazit pozadí FPS", "FPS háttér megjelenítése",
      "Фон под FPS", "FPSの背景を表示", "FPS 배경 표시", "显示 FPS 背景", "顯示 FPS 背景"}},
    {"Drift Camera", {"Drift-Kamera", "Caméra de drift", "Telecamera drift", "Cámara de derrape",
                      "Cámara de derrape", "Câmera de drift", "Driftcamera", "Kamera driftu",
                      "Driftová kamera", "Drift kamera", "Камера дрифта", "ドリフトカメラ",
                      "드리프트 카메라", "漂移镜头", "甩尾鏡頭"}},
    {"Drift Camera Sensitivity",
     {"Drift-Kamera: Empfindlichkeit", "Caméra de drift : sensibilité",
      "Sensibilità telecamera drift", "Sensibilidad de la cámara de derrape",
      "Sensibilidad de la cámara de derrape", "Sensibilidade da câmera de drift",
      "Gevoeligheid driftcamera", "Czułość kamery driftu", "Citlivost driftové kamery",
      "Drift kamera érzékenysége", "Чувствительность камеры дрифта", "ドリフトカメラの感度",
      "드리프트 카메라 감도", "漂移镜头灵敏度", "甩尾鏡頭靈敏度"}},
    {"Drift Camera Speed",
     {"Drift-Kamera: Geschwindigkeit", "Caméra de drift : vitesse", "Velocità telecamera drift",
      "Velocidad de la cámara de derrape", "Velocidad de la cámara de derrape",
      "Velocidade da câmera de drift", "Snelheid driftcamera", "Szybkość kamery driftu",
      "Rychlost driftové kamery", "Drift kamera sebessége", "Скорость камеры дрифта",
      "ドリフトカメラの速度", "드리프트 카메라 속도", "漂移镜头速度", "甩尾鏡頭速度"}},
    {"Drift Camera Range",
     {"Drift-Kamera: Ausschlag", "Caméra de drift : amplitude", "Ampiezza telecamera drift",
      "Amplitud de la cámara de derrape", "Amplitud de la cámara de derrape",
      "Amplitude da câmera de drift", "Bereik driftcamera", "Zakres kamery driftu",
      "Rozsah driftové kamery", "Drift kamera tartománya", "Диапазон камеры дрифта",
      "ドリフトカメラの範囲", "드리프트 카메라 범위", "漂移镜头范围", "甩尾鏡頭範圍"}},
    {"4K Liveries", {"4K-Lackierungen", "Livrées 4K", "Livree 4K", "Diseños 4K", "Diseños 4K",
                     "Pinturas 4K", "4K-ontwerpen", "Malowania 4K", "Polepy 4K", "4K fényezések",
                     "Раскраски 4K", "4Kペイント", "4K 도색", "4K 涂装", "4K 塗裝"}},
    {"Livery 4x MSAA", {"Lackierung 4x MSAA", "Livrée MSAA 4x", "Livrea MSAA 4x",
                        "Diseño MSAA 4x", "Diseño MSAA 4x", "Pintura MSAA 4x", "Ontwerp 4x MSAA",
                        "Malowanie MSAA 4x", "Polep 4x MSAA", "Fényezés 4x MSAA",
                        "Раскраска: MSAA 4x", "ペイント 4x MSAA", "도색 4x MSAA", "涂装 4x MSAA",
                        "塗裝 4x MSAA"}},
    {"Livery Supersampling",
     {"Lackierung-Supersampling", "Suréchantillonnage des livrées", "Supersampling livree",
      "Supermuestreo de diseños", "Supermuestreo de diseños", "Supersampling da pintura",
      "Supersampling ontwerpen", "Supersampling malowań", "Supersampling polepů",
      "Fényezés szupermintavételezés", "Суперсэмплинг раскрасок", "ペイントのスーパーサンプリング",
      "도색 슈퍼샘플링", "涂装超采样", "塗裝超取樣"}},
    /* Settings descriptions (after "Extra Options: ") */
    {"field of view of the near chase camera.",
     {"Sichtfeld der nahen Verfolgerkamera.", "champ de vision de la caméra de poursuite proche.",
      "campo visivo della telecamera di inseguimento vicina.",
      "campo de visión de la cámara de persecución cercana.",
      "campo de visión de la cámara de persecución cercana.",
      "campo de visão da câmera de perseguição próxima.",
      "gezichtsveld van de nabije achtervolgingscamera.", "pole widzenia bliskiej kamery pościgowej.",
      "zorné pole blízké pronásledovací kamery.", "a közeli üldözőkamera látószöge.",
      "угол обзора ближней камеры погони.", "近距離追従カメラの視野角。",
      "근거리 추적 카메라의 시야각.", "近距离追尾镜头的视野。", "近距離追尾鏡頭的視野。"}},
    {"field of view of the far chase camera.",
     {"Sichtfeld der fernen Verfolgerkamera.",
      "champ de vision de la caméra de poursuite lointaine.",
      "campo visivo della telecamera di inseguimento lontana.",
      "campo de visión de la cámara de persecución lejana.",
      "campo de visión de la cámara de persecución lejana.",
      "campo de visão da câmera de perseguição distante.",
      "gezichtsveld van de verre achtervolgingscamera.",
      "pole widzenia dalekiej kamery pościgowej.", "zorné pole vzdálené pronásledovací kamery.",
      "a távoli üldözőkamera látószöge.", "угол обзора дальней камеры погони.",
      "遠距離追従カメラの視野角。", "원거리 추적 카메라의 시야각.", "远距离追尾镜头的视野。",
      "遠距離追尾鏡頭的視野。"}},
    {"field of view of the hood camera.",
     {"Sichtfeld der Motorhaubenkamera.", "champ de vision de la caméra capot.",
      "campo visivo della telecamera sul cofano.", "campo de visión de la cámara del capó.",
      "campo de visión de la cámara del cofre.", "campo de visão da câmera do capô.",
      "gezichtsveld van de motorkapcamera.", "pole widzenia kamery na masce.",
      "zorné pole kamery na kapotě.", "a motorháztető-kamera látószöge.",
      "угол обзора камеры на капоте.", "ボンネットカメラの視野角。", "보닛 카메라의 시야각.",
      "引擎盖镜头的视野。", "引擎蓋鏡頭的視野。"}},
    {"field of view of the bumper camera.",
     {"Sichtfeld der Stoßstangenkamera.", "champ de vision de la caméra pare-chocs.",
      "campo visivo della telecamera sul paraurti.", "campo de visión de la cámara del parachoques.",
      "campo de visión de la cámara de la defensa.", "campo de visão da câmera do para-choque.",
      "gezichtsveld van de bumpercamera.", "pole widzenia kamery na zderzaku.",
      "zorné pole kamery na nárazníku.", "a lökhárító-kamera látószöge.",
      "угол обзора камеры на бампере.", "バンパーカメラの視野角。", "범퍼 카메라의 시야각.",
      "保险杠镜头的视野。", "保險桿鏡頭的視野。"}},
    {"field of view of the cockpit camera.",
     {"Sichtfeld der Cockpitkamera.", "champ de vision de la caméra cockpit.",
      "campo visivo della telecamera abitacolo.", "campo de visión de la cámara de cabina.",
      "campo de visión de la cámara de cabina.", "campo de visão da câmera do cockpit.",
      "gezichtsveld van de cockpitcamera.", "pole widzenia kamery w kokpicie.",
      "zorné pole kamery z kokpitu.", "a pilótafülke-kamera látószöge.",
      "угол обзора камеры из кокпита.", "コックピットカメラの視野角。", "콕핏 카메라의 시야각.",
      "驾驶舱镜头的视野。", "駕駛艙鏡頭的視野。"}},
    {"turn the minimap with the right-stick look camera.",
     {"die Minikarte dreht sich mit der Umsehkamera des rechten Sticks.",
      "la mini-carte tourne avec la caméra libre du stick droit.",
      "la minimappa ruota con la visuale libera della levetta destra.",
      "el minimapa gira con la cámara libre del stick derecho.",
      "el minimapa gira con la cámara libre de la palanca derecha.",
      "o minimapa gira com a câmera livre do analógico direito.",
      "de minikaart draait mee met de rondkijkcamera van de rechterstick.",
      "minimapa obraca się razem z kamerą prawej gałki.",
      "minimapa se otáčí s rozhlížením pravou páčkou.",
      "a minitérkép a jobb kar körbenézős kamerájával együtt fordul.",
      "миникарта поворачивается вместе с обзором правого стика.",
      "右スティックの視点操作に合わせてミニマップを回転します。",
      "오른쪽 스틱 시점에 맞춰 미니맵을 회전합니다.", "小地图随右摇杆环视镜头旋转。",
      "小地圖隨右搖桿環視鏡頭旋轉。"}},
    {"switch to slow orbiting cinematic shots with the HUD hidden after a few seconds of "
     "Autodrive. Driving input brings your camera and HUD back.",
     {"nach einigen Sekunden Autopilot wechselt die Kamera zu langsamen Kinoaufnahmen ohne HUD. "
      "Eine Fahreingabe bringt Kamera und HUD zurück.",
      "après quelques secondes de conduite auto, passe à des plans cinématiques lents sans HUD. "
      "Toute commande de conduite rétablit la caméra et le HUD.",
      "dopo alcuni secondi di guida automatica passa a lente inquadrature cinematiche senza HUD. "
      "Qualsiasi comando di guida ripristina telecamera e HUD.",
      "tras unos segundos de conducción automática, cambia a tomas cinematográficas lentas sin "
      "HUD. Cualquier control de conducción restaura la cámara y el HUD.",
      "tras unos segundos de conducción automática, cambia a tomas cinematográficas lentas sin "
      "HUD. Cualquier control de manejo restaura la cámara y el HUD.",
      "após alguns segundos de direção automática, alterna para tomadas cinematográficas lentas "
      "sem HUD. Qualquer comando de direção restaura a câmera e o HUD.",
      "schakelt na een paar seconden automatisch rijden over op langzame filmische shots zonder "
      "HUD. Stuurinvoer brengt camera en HUD terug.",
      "po kilku sekundach autopilota przełącza na powolne ujęcia filmowe bez HUD. Dowolne "
      "sterowanie przywraca kamerę i HUD.",
      "po několika sekundách autopilota přepne na pomalé filmové záběry bez HUD. Jakékoli řízení "
      "vrátí kameru i HUD.",
      "néhány másodpercnyi önvezetés után lassú, HUD nélküli filmes nézetekre vált. Bármilyen "
      "vezetési bemenet visszahozza a kamerát és a HUD-ot.",
      "через несколько секунд автопилота включаются медленные кинокадры без HUD. Любое "
      "управление возвращает камеру и HUD.",
      "自動運転開始から数秒後、HUDを隠したスローなシネマカメラに切り替えます。運転操作でカメラとHUDが戻ります。",
      "자동 주행 몇 초 후 HUD를 숨긴 느린 시네마틱 화면으로 전환합니다. 주행 입력 시 카메라와 "
      "HUD가 돌아옵니다.",
      "自动驾驶数秒后切换为隐藏 HUD 的慢速环绕电影镜头。任何驾驶操作都会恢复镜头和 HUD。",
      "自動駕駛數秒後切換為隱藏 HUD 的慢速環繞電影鏡頭。任何駕駛操作都會恢復鏡頭和 HUD。"}},
    {"frame rate in the top-right corner of the HUD and menus.",
     {"Bildrate oben rechts im HUD und in Menüs.",
      "fréquence d'images en haut à droite du HUD et des menus.",
      "frequenza fotogrammi nell'angolo in alto a destra di HUD e menu.",
      "fotogramas por segundo en la esquina superior derecha del HUD y los menús.",
      "cuadros por segundo en la esquina superior derecha del HUD y los menús.",
      "taxa de quadros no canto superior direito do HUD e dos menus.",
      "framerate rechtsboven in de HUD en menu's.",
      "liczba klatek w prawym górnym rogu HUD i menu.",
      "snímková frekvence v pravém horním rohu HUD a nabídek.",
      "képkockasebesség a HUD és a menük jobb felső sarkában.",
      "частота кадров в правом верхнем углу HUD и меню.",
      "HUDとメニューの右上にフレームレートを表示します。",
      "HUD와 메뉴 오른쪽 위에 프레임 속도를 표시합니다.", "在 HUD 和菜单右上角显示帧率。",
      "在 HUD 和選單右上角顯示影格率。"}},
    {"add the total GPU 3D load to the frame rate readout.",
     {"zeigt zusätzlich die gesamte 3D-Auslastung der GPU an.",
      "ajoute la charge 3D totale du GPU à l'affichage.",
      "aggiunge il carico 3D totale della GPU al contatore.",
      "añade la carga 3D total de la GPU al indicador.",
      "agrega la carga 3D total de la GPU al indicador.",
      "adiciona a carga 3D total da GPU ao contador.",
      "voegt de totale 3D-belasting van de GPU toe.",
      "dodaje całkowite obciążenie 3D GPU do licznika.",
      "přidá celkové 3D vytížení GPU k ukazateli.",
      "a kijelzéshez hozzáadja a GPU teljes 3D-terhelését.",
      "добавляет общую 3D-загрузку GPU к счётчику.",
      "フレームレート表示にGPUの3D使用率を追加します。",
      "프레임 속도 표시에 GPU 3D 사용률을 추가합니다.", "在帧率显示中加入 GPU 3D 总占用。",
      "在影格率顯示中加入 GPU 3D 總使用率。"}},
    {"add the average frame time in milliseconds to the frame rate readout.",
     {"zeigt zusätzlich die durchschnittliche Bildzeit in Millisekunden an.",
      "ajoute le temps d'image moyen en millisecondes à l'affichage.",
      "aggiunge il tempo medio per fotogramma in millisecondi al contatore.",
      "añade el tiempo medio de fotograma en milisegundos al indicador.",
      "agrega el tiempo promedio de cuadro en milisegundos al indicador.",
      "adiciona o tempo médio de quadro em milissegundos ao contador.",
      "voegt de gemiddelde frametijd in milliseconden toe.",
      "dodaje średni czas klatki w milisekundach do licznika.",
      "přidá průměrný čas snímku v milisekundách k ukazateli.",
      "a kijelzéshez hozzáadja az átlagos képkocka-időt ezredmásodpercben.",
      "добавляет среднее время кадра в миллисекундах к счётчику.",
      "フレームレート表示に平均フレームタイム（ミリ秒）を追加します。",
      "프레임 속도 표시에 평균 프레임 시간(밀리초)을 추가합니다.",
      "在帧率显示中加入平均帧时间（毫秒）。", "在影格率顯示中加入平均影格時間（毫秒）。"}},
    {"draw the frame rate readout on a dark box; off shows the text alone.",
     {"zeigt die Bildrate auf einem dunklen Kasten; aus zeigt nur den Text.",
      "affiche le compteur sur un fond sombre ; désactivé, le texte seul.",
      "mostra il contatore su un riquadro scuro; disattivato mostra solo il testo.",
      "muestra el indicador sobre un recuadro oscuro; desactivado muestra solo el texto.",
      "muestra el indicador sobre un recuadro oscuro; desactivado muestra solo el texto.",
      "mostra o contador sobre uma caixa escura; desligado mostra só o texto.",
      "toont de framerate op een donker vlak; uit toont alleen de tekst.",
      "wyświetla licznik na ciemnym tle; wyłączone pokazuje sam tekst.",
      "zobrazí ukazatel na tmavém pozadí; vypnuto zobrazí jen text.",
      "sötét háttéren jeleníti meg a kijelzést; kikapcsolva csak a szöveg látszik.",
      "показывает счётчик на тёмной подложке; выкл. — только текст.",
      "フレームレート表示に暗い背景を付けます。オフでは文字のみ表示します。",
      "프레임 속도 표시에 어두운 배경을 넣습니다. 끄면 글자만 표시합니다.",
      "在深色背景框上显示帧率；关闭时仅显示文字。", "在深色背景框上顯示影格率；關閉時僅顯示文字。"}},
    {"chase cameras swing toward the direction of travel while the car slides.",
     {"Verfolgerkameras schwenken beim Driften in Fahrtrichtung.",
      "les caméras de poursuite pivotent vers la direction de déplacement pendant les glissades.",
      "le telecamere di inseguimento ruotano verso la direzione di marcia durante le derapate.",
      "las cámaras de persecución giran hacia la dirección de avance mientras el coche derrapa.",
      "las cámaras de persecución giran hacia la dirección de avance mientras el auto derrapa.",
      "as câmeras de perseguição giram para a direção do movimento enquanto o carro derrapa.",
      "achtervolgingscamera's draaien mee met de rijrichting terwijl de auto glijdt.",
      "kamery pościgowe obracają się w kierunku jazdy podczas poślizgu.",
      "pronásledovací kamery se během smyku natáčejí ve směru jízdy.",
      "csúszás közben az üldözőkamerák a haladási irány felé fordulnak.",
      "камеры погони поворачиваются по направлению движения во время заноса.",
      "車が滑っている間、追従カメラが進行方向へ振れます。",
      "차량이 미끄러지는 동안 추적 카메라가 진행 방향으로 돌아갑니다.",
      "车辆侧滑时，追尾镜头会转向行进方向。", "車輛側滑時，追尾鏡頭會轉向行進方向。"}},
    {"how much of the slide angle the drift camera follows.",
     {"wie stark die Drift-Kamera dem Driftwinkel folgt.",
      "proportion de l'angle de glisse suivie par la caméra de drift.",
      "quanto dell'angolo di derapata segue la telecamera drift.",
      "cuánto del ángulo de derrape sigue la cámara de derrape.",
      "cuánto del ángulo de derrape sigue la cámara de derrape.",
      "quanto do ângulo de derrapagem a câmera de drift acompanha.",
      "hoeveel van de slipwinkel de driftcamera volgt.",
      "jak bardzo kamera driftu podąża za kątem poślizgu.",
      "jak moc driftová kamera sleduje úhel smyku.",
      "a drift kamera mennyire követi a csúszás szögét.",
      "насколько камера дрифта следует за углом заноса.",
      "ドリフトカメラがスライド角にどれだけ追従するか。",
      "드리프트 카메라가 미끄러짐 각도를 따라가는 정도.", "漂移镜头跟随侧滑角度的程度。",
      "甩尾鏡頭跟隨側滑角度的程度。"}},
    {"how quickly the drift camera swings into and out of a slide.",
     {"wie schnell die Drift-Kamera in den Drift hinein- und wieder herausschwenkt.",
      "vitesse à laquelle la caméra de drift pivote en entrée et en sortie de glisse.",
      "quanto velocemente la telecamera drift ruota in entrata e in uscita dalla derapata.",
      "qué tan rápido gira la cámara de derrape al entrar y salir de un derrape.",
      "qué tan rápido gira la cámara de derrape al entrar y salir de un derrape.",
      "quão rápido a câmera de drift gira ao entrar e sair de uma derrapagem.",
      "hoe snel de driftcamera in en uit een slip draait.",
      "jak szybko kamera driftu obraca się przy wejściu w poślizg i wyjściu z niego.",
      "jak rychle se driftová kamera natáčí do smyku a zpět.",
      "milyen gyorsan fordul a drift kamera a csúszásba és vissza.",
      "как быстро камера дрифта поворачивается при входе в занос и выходе из него.",
      "スライドの開始・終了時にドリフトカメラが振れる速さ。",
      "미끄러짐 진입과 탈출 시 드리프트 카메라가 돌아가는 속도.",
      "进入和退出侧滑时漂移镜头的转动速度。", "進入和退出側滑時甩尾鏡頭的轉動速度。"}},
    {"the largest angle the drift camera swings away from the car.",
     {"der größte Winkel, um den die Drift-Kamera vom Auto wegschwenkt.",
      "angle maximal dont la caméra de drift s'écarte de la voiture.",
      "l'angolo massimo di cui la telecamera drift si allontana dall'auto.",
      "el ángulo máximo que la cámara de derrape se aleja del coche.",
      "el ángulo máximo que la cámara de derrape se aleja del auto.",
      "o maior ângulo em que a câmera de drift se afasta do carro.",
      "de grootste hoek waarover de driftcamera van de auto wegdraait.",
      "największy kąt, o jaki kamera driftu odchyla się od samochodu.",
      "největší úhel, o který se driftová kamera vychýlí od auta.",
      "a legnagyobb szög, amennyire a drift kamera elfordul az autótól.",
      "максимальный угол отклонения камеры дрифта от машины.",
      "ドリフトカメラが車から振れる最大角度。", "드리프트 카메라가 차량에서 벗어나는 최대 각도.",
      "漂移镜头偏离车辆的最大角度。", "甩尾鏡頭偏離車輛的最大角度。"}},
    {"render car liveries at 4096x4096 instead of 1024 or 2048. Applies after restarting the "
     "game.",
     {"rendert Lackierungen in 4096x4096 statt 1024 oder 2048. Gilt nach einem Neustart des "
      "Spiels.",
      "affiche les livrées en 4096x4096 au lieu de 1024 ou 2048. Prend effet au redémarrage du jeu.",
      "renderizza le livree a 4096x4096 invece di 1024 o 2048. Valido dopo il riavvio del gioco.",
      "renderiza los diseños a 4096x4096 en lugar de 1024 o 2048. Se aplica al reiniciar el juego.",
      "renderiza los diseños a 4096x4096 en lugar de 1024 o 2048. Se aplica al reiniciar el juego.",
      "renderiza as pinturas em 4096x4096 em vez de 1024 ou 2048. Vale após reiniciar o jogo.",
      "rendert ontwerpen in 4096x4096 in plaats van 1024 of 2048. Werkt na het herstarten van de "
      "game.",
      "renderuje malowania w 4096x4096 zamiast 1024 lub 2048. Działa po ponownym uruchomieniu gry.",
      "vykresluje polepy v rozlišení 4096x4096 místo 1024 nebo 2048. Projeví se po restartu hry.",
      "a fényezéseket 4096x4096-os felbontásban rendereli 1024 vagy 2048 helyett. A játék "
      "újraindítása után lép érvénybe.",
      "раскраски рендерятся в 4096x4096 вместо 1024 или 2048. Вступает в силу после перезапуска "
      "игры.",
      "ペイントを1024/2048ではなく4096x4096で描画します。ゲームの再起動後に反映されます。",
      "도색을 1024 또는 2048 대신 4096x4096으로 렌더링합니다. 게임을 다시 시작하면 적용됩니다.",
      "以 4096x4096（而非 1024 或 2048）渲染涂装。重启游戏后生效。",
      "以 4096x4096（而非 1024 或 2048）算繪塗裝。重新啟動遊戲後生效。"}},
    {"smooth livery and vinyl edges with 4x MSAA instead of 2x. Applies after restarting the "
     "game.",
     {"glättet Lackierungs- und Vinylkanten mit 4x MSAA statt 2x. Gilt nach einem Neustart des "
      "Spiels.",
      "lisse les bords des livrées et des vinyles en MSAA 4x au lieu de 2x. Prend effet au "
      "redémarrage du jeu.",
      "smussa i bordi di livree e vinili con MSAA 4x invece di 2x. Valido dopo il riavvio del "
      "gioco.",
      "suaviza los bordes de diseños y vinilos con MSAA 4x en lugar de 2x. Se aplica al reiniciar "
      "el juego.",
      "suaviza los bordes de diseños y vinilos con MSAA 4x en lugar de 2x. Se aplica al reiniciar "
      "el juego.",
      "suaviza as bordas de pinturas e vinis com MSAA 4x em vez de 2x. Vale após reiniciar o jogo.",
      "maakt randen van ontwerpen en vinyls glad met 4x MSAA in plaats van 2x. Werkt na het "
      "herstarten van de game.",
      "wygładza krawędzie malowań i naklejek za pomocą MSAA 4x zamiast 2x. Działa po ponownym "
      "uruchomieniu gry.",
      "vyhlazuje okraje polepů a vinylů pomocí 4x MSAA místo 2x. Projeví se po restartu hry.",
      "a fényezések és matricák széleit 4x MSAA-val simítja 2x helyett. A játék újraindítása után "
      "lép érvénybe.",
      "сглаживает края раскрасок и винилов MSAA 4x вместо 2x. Вступает в силу после перезапуска "
      "игры.",
      "ペイントとバイナルの縁を2xではなく4x MSAAで滑らかにします。ゲームの再起動後に反映されます。",
      "도색과 비닐 가장자리를 2x 대신 4x MSAA로 부드럽게 합니다. 게임을 다시 시작하면 적용됩니다.",
      "使用 4x MSAA（而非 2x）平滑涂装和贴纸边缘。重启游戏后生效。",
      "使用 4x MSAA（而非 2x）平滑塗裝和貼紙邊緣。重新啟動遊戲後生效。"}},
    {"render liveries at twice the resolution before downsampling (heavier). Applies after "
     "restarting the game.",
     {"rendert Lackierungen vor dem Verkleinern in doppelter Auflösung (aufwendiger). Gilt nach "
      "einem Neustart des Spiels.",
      "affiche les livrées en double résolution avant réduction (plus lourd). Prend effet au "
      "redémarrage du jeu.",
      "renderizza le livree a risoluzione doppia prima del ridimensionamento (più pesante). Valido "
      "dopo il riavvio del gioco.",
      "renderiza los diseños al doble de resolución antes de reducirlos (más exigente). Se aplica "
      "al reiniciar el juego.",
      "renderiza los diseños al doble de resolución antes de reducirlos (más exigente). Se aplica "
      "al reiniciar el juego.",
      "renderiza as pinturas no dobro da resolução antes de reduzir (mais pesado). Vale após "
      "reiniciar o jogo.",
      "rendert ontwerpen op dubbele resolutie voor het verkleinen (zwaarder). Werkt na het "
      "herstarten van de game.",
      "renderuje malowania w podwójnej rozdzielczości przed zmniejszeniem (bardziej wymagające). "
      "Działa po ponownym uruchomieniu gry.",
      "vykresluje polepy ve dvojnásobném rozlišení před zmenšením (náročnější). Projeví se po "
      "restartu hry.",
      "a fényezéseket kicsinyítés előtt dupla felbontásban rendereli (erőforrásigényesebb). A "
      "játék újraindítása után lép érvénybe.",
      "раскраски рендерятся в двойном разрешении перед уменьшением (тяжелее). Вступает в силу "
      "после перезапуска игры.",
      "ペイントを2倍の解像度で描画してから縮小します（負荷大）。ゲームの再起動後に反映されます。",
      "도색을 두 배 해상도로 렌더링한 뒤 축소합니다(부하 큼). 게임을 다시 시작하면 적용됩니다.",
      "先以两倍分辨率渲染涂装再缩小（负载更高）。重启游戏后生效。",
      "先以兩倍解析度算繪塗裝再縮小（負載更高）。重新啟動遊戲後生效。"}},
};

static int fallback_language = EN;
static volatile LONG game_language = -1;
static int (*game_code)(char [4]);

static int same(const char *left, const char *right) {
    size_t length = text_length(right);
    return text_length(left) == length && memory_equal(left, right, length);
}

/* Maps a language name (an FH3 code such as CHT, or BCP-47 such as pt-BR,
   zh-Hant-TW, es-419) to a table; -1 when unknown. */
static int language_from(const wchar_t *name) {
    char text[32];
    unsigned length = 0;
    for (; length < sizeof(text) - 1 && name[length]; ++length) {
        wchar_t c = name[length];
        if (c > 127) return -1;
        text[length] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c == '_' ? '-' : c);
    }
    text[length] = 0;
    static const struct {
        const char *name;
        int language;
    } names[] = {
        {"en", EN},       {"gb", EN},        {"de", DE},        {"fr", FR},     {"it", IT},
        {"es", ES},       {"mx", MX},        {"pt", BR},        {"br", BR},     {"nl", NL},
        {"pl", PL},       {"cs", CZ},        {"cz", CZ},        {"hu", HU},     {"ru", RU},
        {"ja", JP},       {"jp", JP},        {"ko", KO},        {"chs", CHS},   {"cht", CHT},
        {"zh-hans", CHS}, {"zh-cn", CHS},    {"zh-sg", CHS},    {"zh-hant", CHT}, {"zh-tw", CHT},
        {"zh-hk", CHT},   {"zh-mo", CHT},    {"zh", CHS},
    };
    if (!length || same(text, "auto") || same(text, "system")) return -1;
    /* Spanish outside Spain uses the Mexican table, as the game does. */
    if (length > 3 && text[0] == 'e' && text[1] == 's' && text[2] == '-' && !same(text, "es-es"))
        return MX;
    for (unsigned i = 0; i < ARRAY_COUNT(names); ++i) {
        size_t n = text_length(names[i].name);
        if (length >= n && memory_equal(text, names[i].name, n) &&
            (length == n || text[n] == '-'))
            return names[i].language;
    }
    return -1;
}

typedef BOOL(WINAPI *PreferredLanguages)(DWORD, PULONG, PZZWSTR, PULONG);

void fh3_text_init(const wchar_t *directory, int (*code)(char [4])) {
    wchar_t value[64];
    int chosen = -1;
    game_code = code;
    InterlockedExchange(&game_language, -1);
    fallback_language = EN;
    {
        /* Azimuth's language for the game, when one is set instead of the system's. */
        static wchar_t azimuth[PATH_CAPACITY]; /* static: too large for the stack */
        if (path_join(azimuth, directory, L"data\\azimuth.ini")) {
            GetPrivateProfileStringW(L"title-4ce1dfab", L"language", L"", value,
                                     ARRAY_COUNT(value), azimuth);
            chosen = language_from(value);
            if (chosen < 0) {
                GetPrivateProfileStringW(L"launcher", L"language", L"", value, ARRAY_COUNT(value),
                                         azimuth);
                chosen = language_from(value);
            }
        }
    }
    if (chosen < 0) {
        /* The Windows display languages, in order, as Azimuth reads them. */
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        PreferredLanguages preferred =
            kernel ? (PreferredLanguages)(uintptr_t)GetProcAddress(kernel, "GetUserPreferredUILanguages")
                   : NULL;
        static wchar_t list[512];
        ULONG count = 0, size = ARRAY_COUNT(list);
        if (preferred && preferred(MUI_LANGUAGE_NAME, &count, list, &size))
            for (const wchar_t *name = list; *name && chosen < 0;) {
                chosen = language_from(name);
                while (*name) ++name;
                ++name;
            }
    }
    if (chosen >= 0) fallback_language = chosen;
}

int fh3_text_language(void) {
    LONG current = game_language;
    char code[4] = {0};
    if (current < 0 && game_code && game_code(code)) {
        wchar_t wide[4] = {0};
        for (unsigned i = 0; i < 3; ++i) wide[i] = (unsigned char)code[i];
        int found = language_from(wide);
        current = found < 0 ? EN : found;
        LONG prior = InterlockedCompareExchange(&game_language, current, -1);
        if (prior >= 0) current = prior;
    }
    return current < 0 ? fallback_language : (int)current;
}

const char *fh3_text_code(int language) { return codes[language]; }
int fh3_text_from_game(void) { return game_language >= 0; }

static const char *translation(const char *english, size_t length, int language) {
    if (language == EN) return NULL;
    for (unsigned i = 0; i < ARRAY_COUNT(texts); ++i)
        if (text_length(texts[i].english) == length && memory_equal(english, texts[i].english, length))
            return texts[i].text[language];
    return NULL;
}

unsigned fh3_text_wide(const char *english, wchar_t *out, unsigned capacity) {
    const char *value = translation(english, text_length(english), fh3_text_language());
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value ? value : english,
                                   -1, out, (int)capacity);
    if (!size && value)
        size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, english, -1, out, (int)capacity);
    if (!size) out[0] = 0;
    return size > 0 ? (unsigned)(size - 1) : 0;
}

static void append(char *out, size_t *used, const char *text, size_t length) {
    if (out) memory_copy(out + *used, text, length);
    *used += length;
}

static void append_xml(char *out, size_t *used, const wchar_t *text) {
    while (*text) {
        unsigned code = *text++;
        if (code >= 0xd800 && code <= 0xdbff) {
            code = 0x10000 + ((code - 0xd800) << 10) + (*text++ - 0xdc00);
        }
        if (code < 0x80 && code != '&' && code != '"' && code != '<' && code != '>') {
            char character = (char)code;
            append(out, used, &character, 1);
        } else {
            char reference[10] = "&#x";
            unsigned digits = 1;
            for (unsigned remaining = code >> 4; remaining; remaining >>= 4) ++digits;
            for (unsigned i = 0; i < digits; ++i)
                reference[3 + i] = "0123456789ABCDEF"[(code >> (4 * (digits - i - 1))) & 15];
            reference[3 + digits] = ';';
            append(out, used, reference, 4 + digits);
        }
    }
}

static size_t translate_xaml(const char *xaml, int language, char *out, int *changed) {
    static const char prefix[] = "Extra Options: ";
    const char *end = xaml + text_length(xaml);
    size_t used = 0;
    for (const char *at = xaml; at < end;) {
        const char *start = text_find(at, (size_t)(end - at), "=\"");
        if (!start) {
            append(out, &used, at, (size_t)(end - at));
            break;
        }
        start += 2;
        const char *quote = text_find(start, (size_t)(end - start), "\"");
        if (!quote) {
            append(out, &used, at, (size_t)(end - at));
            break;
        }
        if ((size_t)(quote - start) >= sizeof(prefix) - 1 &&
            memory_equal(start, prefix, sizeof(prefix) - 1))
            start += sizeof(prefix) - 1;
        const char *value = translation(start, (size_t)(quote - start), language);
        wchar_t wide[512];
        if (value && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                                         wide, ARRAY_COUNT(wide))) {
            append(out, &used, at, (size_t)(start - at));
            append_xml(out, &used, wide);
            append(out, &used, quote, 1);
            *changed = 1;
        } else {
            append(out, &used, at, (size_t)(quote + 1 - at));
        }
        at = quote + 1;
    }
    return used;
}

char *fh3_text_xaml(const char *xaml, int language) {
    if (language == EN || !*xaml) return NULL;
    int changed = 0;
    size_t length = translate_xaml(xaml, language, NULL, &changed);
    if (!changed) return NULL;
    char *out = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (out) out[translate_xaml(xaml, language, out, &changed)] = 0;
    return out;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) const char *Fh3TestTextLanguage(void) {
    return fh3_text_code(fh3_text_language());
}
__declspec(dllexport) unsigned Fh3TestTextWide(const char *key, wchar_t *out, unsigned capacity) {
    return fh3_text_wide(key, out, capacity);
}
__declspec(dllexport) char *Fh3TestTextXaml(const char *xaml) {
    return fh3_text_xaml(xaml, fh3_text_language());
}
#endif

