#include "video_options_text.h"
#include "game_addresses.h"
#include "support.h"

static const VideoOptionsText english = {
    .photo_lights = L"Headlights",
    .player_character = "Player Character",
    .player_character_description = "Choose the player model. Default follows the game. Changes apply when the player model next loads.",
    .open_world_drivatars = "Open World Drivatars",
    .open_world_drivatars_description = "Enable Drivatars in free roam. Requires a game restart to take effect. Does not affect race opponents or event rewards.",
    .fov_label = "FOV",
    .skill_counter_shake = "Skill Counter Shake",
    .skill_counter_shake_description = "Enable shaking and size growth on the skill-chain counter.",
    .speed_zone_vignette = "Speed Zone Vignette",
    .speed_zone_vignette_description = "Enable the extra vignette shown during speed zones.",
    .unit_names = {"Power Units", "Spring Units"},
    .unit_descriptions = {
        "Choose the units used to display engine power. Default follows the Units setting.",
        "Choose the units used to display spring stiffness. Default follows the Units setting."},
    .skill_point_notification = "Skill Point Earned Notification",
    .skill_point_description = "Show the Skill Point Earned notification and play its accompanying dialogue. Skill points are still awarded when disabled.",
    .master_volume = "Master Volume",
    .manufacturer_jump = L"Jump to manufacturer",
    .event_labels = {L"Drivatar Count", L"Grid Order", L"Stock Cars Only", L"Traffic"},
    .grid_choices = {L"Random", L"Lowest PI First", L"Highest PI First"},
    .division_label = L"Car Division",
    .garage_label = L"Return to Garage",
    .garage_question = L"Return to the festival garage on the current map?",
    .camera = "Drift Camera",
    .driver = "Driver",
    .parameters = {"Sensitivity", "Look Speed", "Range"},
    .descriptions = {"Enable the drift camera for chase and cockpit views, use it only in "
                     "the cockpit, or disable it.",
                     "The ratio of car angle to camera angle. Increase this setting to "
                     "increase how sensitive the "
                     "camera is to car angle.",
                     "The speed at which the camera moves from its original position "
                     "towards "
                     "the direction of "
                     "travel. Reducing this setting will smooth out the camera's movement "
                     "due "
                     "to changes in "
                     "direction.",
                     "Reduce this setting to limit the camera's movement when the car is "
                     "at "
                     "peak angle."},
    .choices = {L"On", L"Driver", L"Off"},
    .fov_views = {"Chase", "Far Chase", "Driver", "Hood", "Bumper"},
    .fov_description = "Set the vertical field of view for the given camera in degrees.",
    .settings_names = {"Real-time Shadows", "", "Photo Mode Samples", "", "Minimap Orientation",
                       "Autodrive Cinematic Camera", "Skip Championship Dialogue", "Spawn Location",
                       "Change Car Position", "4K Resolution Liveries", "Enhanced Livery MSAA",
                       "Enhanced Livery Sampling", "Show All Events", "Autodrive in Events",
                       "Custom Showcases", "Ben's Photo Mode Dialogue", "Ashley's Garage Dialogue",
                       "Wheelspin Notification", "Checkpoint Sound Effect"},
    .settings_descriptions =
        {"Control smooth sun-shadow updates. Default uses the current "
         "translation layer's setting. "
         "Restart required.",
         "",
         "Set photo mode samples. Default uses the current translation layer's "
         "setting. Restart "
         "required.",
         "", "Choose whether the minimap follows the car or the camera.",
         "Use cinematic cameras during Autodrive.",
         "Skip the forced dialogue when entering a championship race in an "
         "ineligible car. Restart "
         "required.",
         "Choose the spawn location used when booting the game. Default spawns "
         "you "
         "at the nearest car "
         "meet relative to when you closed the game as usual. A game restart "
         "is "
         "needed to apply "
         "changes.",
         "Where the new car should be when you change cars in the open world. "
         "The "
         "latter option will "
         "leave you exactly where the old car was.",
         "Use 4K liveries. Default follows the translation layer. Restart to "
         "apply.",
         "Use 4x livery MSAA. Default follows the translation layer. Restart "
         "to apply.",
         "Use 2x livery sampling. Default follows the translation layer. "
         "Restart to apply.",
         "Show all the race set-up points in the map. Functions as a way to start any race in the "
         "game.",
         "Allow Autodrive in races, road trips and other events with a route.",
         "This will let you pick your own car to play showcase events.",
         "Play Ben's dialogue when taking a photo with no newly photographed cars.",
         "Play Ashley's recurring greetings when entering the garage.",
         "Show the free-roam wheelspin prompt and play its accompanying dialogue. Post-race messages and spin rewards stay enabled "
         "when "
         "disabled.",
         "Play the sound effect when crossing a checkpoint."},
    .default_choice = L"Default",
    .minimap_choices = {L"Follow Car", L"Follow Camera"},
    .spawn_choices = {L"Car Meet Entrance", L"Festival Site"},
    .car_position_choices = {L"Nearest Road", L"Same Location"},
    .keyboard_mouse = "KEYBOARD & MOUSE",
    .kbm_names = {"Input Mode", "Mouse Look", "Mouse Look Up/Down", "Invert Mouse Y", "Mouse Sensitivity", "Keyboard Steering Speed", "Menu Pointer", "Cursor Size", "Map Cursor Speed"},
    .kbm_descriptions =
        {"Auto switches to whatever you touched last. Keyboard & Mouse or Gamepad keep one source. Applies at once.",
         "While driving, the mouse turns the camera and the view recenters after a moment. Applies at once.",
         "In the bumper, hood, cockpit and driver views, moving the mouse forward and back tilts the view up and down; it returns like the side-to-side look. Applies at once.",
         "Swaps up and down for the mouse: looking up and down while driving, the photo mode camera and dragging the car viewer. The map cursor is not affected. Applies at once.",
         "How far the view turns per mouse movement. Applies at once.",
         "How quickly the steering keys turn the wheel and let it return. Applies at once.",
         "A pointer in the pause menu and map: click to choose, right click to go back, wheel to scroll. Applies at once.",
         "Size of the menu pointer. Applies at once.",
         "How fast the mouse moves the world map's cursor. Applies at once."},
    .kbm_modes = {L"Auto", L"Keyboard & Mouse", L"Gamepad"},
    .kbm_actions = {"Throttle", "Brake", "Steer Left", "Steer Right", "Handbrake", "Shift Up", "Shift Down", "Clutch", "Change Camera", "Rewind", "Look Back", "Activate", "Photo Mode", "Previous Station", "Next Station", "Map", "Pause", "Menu: Confirm", "Menu: Back", "Menu: Up", "Menu: Down", "Menu: Left", "Menu: Right", "Menu: Previous Tab", "Menu: Next Tab", "Menu: X Button", "Menu: Y Button", "Menu: Left Trigger", "Menu: Right Trigger", "Menu: D-pad Up", "Menu: D-pad Down", "Menu: D-pad Left", "Menu: D-pad Right"},
    .kbm_key_descriptions =
        {"Key for this driving action. Press Enter (or Left/Right) on the row, then the new key; Esc cancels, Backspace or Delete clears it. A key another driving action uses is refused. Applies at once.",
         "Key for this menu action. Press Enter (or Left/Right) on the row, then the new key; Esc cancels, Backspace or Delete clears it. A key another menu action uses is refused. Applies at once."},
    .kbm_reset = "Reset Key Bindings",
    .kbm_reset_description = "Returns every key binding to its default. Press Enter or Left/Right. Applies at once.",
    .kbm_labels = {L"Press a key...", L"None", L"Key not available", L"Keyboard & mouse off", L"Save failed", L"In use", L"Reset", L"Done"}};

static const VideoOptionsText brazilian = {
    .photo_lights = L"Faróis",
    .player_character = "Personagem do jogador",
    .player_character_description = "Escolha o modelo do jogador. Padrão segue o jogo. As alterações são aplicadas na próxima vez que o modelo do jogador for carregado.",
    .open_world_drivatars = "Drivatars no mundo aberto",
    .open_world_drivatars_description = "Ativa os Drivatars no mundo aberto. É necessário reiniciar o jogo para aplicar. Não afeta os adversários das corridas nem as recompensas dos eventos.",
    .fov_label = "Campo de visão",
    .skill_counter_shake = "Tremor do contador de habilidades",
    .skill_counter_shake_description = "Ativar o tremor e o aumento de tamanho do contador da sequência de habilidades.",
    .speed_zone_vignette = "Vinheta de zona de velocidade",
    .speed_zone_vignette_description = "Ativar a vinheta adicional exibida durante as zonas de velocidade.",
    .unit_names = {"Unidades de Potência", "Unidades das Molas"},
    .unit_descriptions = {
        "Escolha as unidades de potência do motor. Padrão segue a configuração de Unidades.",
        "Escolha as unidades de rigidez das molas. Padrão segue a configuração de Unidades."},
    .skill_point_notification = "Notificação de ponto de habilidade",
    .skill_point_description = "Exibir a notificação de ponto de habilidade ganho e sua fala. Os pontos continuam sendo concedidos quando desativada.",
    .master_volume = "Volume geral",
    .manufacturer_jump = L"Ir para fabricante",
    .event_labels = {L"Número de Drivatars", L"Ordem do grid", L"Apenas carros de fábrica",
                     L"Tráfego"},
    .grid_choices = {L"Aleatória", L"Menor IP primeiro", L"Maior IP primeiro"},
    .division_label = L"Divisão de carros",
    .garage_label = L"Voltar à garagem",
    .garage_question = L"Voltar à garagem do festival no mapa atual?",
    .camera = "Câmera de Drift",
    .driver = "Piloto",
    .parameters = {"Sensibilidade", "Velocidade de movimento", "Alcance"},
    .descriptions = {"Ative a Câmera de Drift nas visões de perseguição e do cockpit, "
                     "apenas no cockpit, ou desative-a.",
                     "A proporção entre o ângulo do carro e o ângulo da câmera. Aumente "
                     "esta "
                     "configuração para "
                     "tornar a câmera mais sensível ao ângulo do carro.",
                     "A velocidade com que a câmera se move da posição original em direção "
                     "ao "
                     "movimento do carro. "
                     "Reduzir esta configuração suaviza o movimento da câmera nas mudanças "
                     "de "
                     "direção.",
                     "Reduza esta configuração para limitar o movimento da câmera quando o "
                     "carro estiver no ângulo "
                     "máximo."},
    .choices = {L"Ligado", L"Piloto", L"Desligado"},
    .fov_views = {"Perseguição", "Perseguição distante", "Piloto", "Capô", "Para-choque"},
    .fov_description = "Defina o campo de visão vertical da câmera selecionada em graus.",
    .settings_names = {"Sombras em tempo real", "", "Amostras do modo foto", "",
                       "Orientação do minimapa", "Câmera cinematográfica do Autodrive",
                       "Ignorar diálogo de campeonato", "Local de início",
                       "Posição ao trocar de carro", "Pinturas em resolução 4K",
                       "MSAA aprimorado das pinturas", "Amostragem aprimorada das pinturas",
                       "Mostrar todos os eventos", "Autodrive em eventos",
                       "Exibições personalizadas", "Diálogo do Ben no Modo Foto",
                       "Diálogo da Ashley na garagem", "Notificação de roleta",
                       "Efeito sonoro dos checkpoints"},
    .settings_descriptions =
        {"Controla a suavidade das sombras. Padrão usa a configuração da camada de tradução atual. Reinicie para aplicar.",
         "",
         "Define as amostras do modo foto. Padrão usa a configuração da camada de tradução atual. Reinicie para aplicar.",
         "",
         "Escolhe se o minimapa acompanha o carro ou a câmera.",
         "Usa câmeras cinematográficas durante o Autodrive.",
         "Ignora o diálogo obrigatório ao entrar em uma corrida de campeonato com um carro inelegível. Reinicie o jogo para aplicar.",
         "Escolha o local onde você começa ao iniciar o jogo. Padrão coloca você no encontro de carros mais próximo de onde estava ao fechar o jogo, como de costume. Reinicie o jogo para aplicar as alterações.",
         "Onde o novo carro deve ficar quando você troca de carro no mundo aberto. A segunda opção deixa você exatamente onde o carro anterior estava.",
         "Usa pinturas em 4K. Padrão segue a camada de tradução. Reinicie para aplicar.",
         "Usa MSAA 4x nas pinturas. Padrão segue a camada de tradução. Reinicie para aplicar.",
         "Usa amostragem 2x nas pinturas. Padrão segue a camada de tradução. Reinicie para aplicar.",
         "Mostra todos os pontos de configuração de corridas no mapa. Permite iniciar qualquer corrida do jogo.",
         "Permite usar Autodrive em corridas, viagens e outros eventos com rota.",
         "Permite escolher seu próprio carro para participar dos eventos de exibição.",
         "Reproduz o diálogo do Ben ao tirar uma foto sem carros fotografados pela primeira vez.",
         "Reproduz as saudações recorrentes da Ashley ao entrar na garagem.",
         "Exibe o aviso de roleta no mundo aberto e reproduz a fala correspondente. As mensagens após as corridas e as recompensas da roleta continuam ativas quando esta opção está desativada.",
         "Reproduz o efeito sonoro ao cruzar um checkpoint."},
    .default_choice = L"Padrão",
    .minimap_choices = {L"Seguir carro", L"Seguir câmera"},
    .spawn_choices = {L"Entrada do encontro de carros", L"Local do festival"},
    .car_position_choices = {L"Estrada mais próxima", L"Mesmo local"},
    .keyboard_mouse = "TECLADO E MOUSE",
    .kbm_names = {"Modo de Entrada", "Visão com o Mouse", "Visão com o Mouse Cima/Baixo", "Inverter Eixo Y do Mouse", "Sensibilidade do Mouse", "Velocidade de Esterço no Teclado", "Ponteiro nos Menus", "Tamanho do Cursor", "Velocidade do Cursor no Mapa"},
    .kbm_descriptions =
        {"Automático alterna para o que você usou por último. Teclado e mouse ou Controle mantêm uma única fonte. Aplica-se na hora.",
         "Ao dirigir, o mouse gira a câmera e a visão volta ao centro após um momento. Aplica-se na hora.",
         "Nas visões de para-choque, capô, cockpit e piloto, mover o mouse para a frente e para trás inclina a visão para cima e para baixo; ela volta como a visão lateral. Aplica-se na hora.",
         "Inverte cima e baixo no mouse: olhar para cima e para baixo ao dirigir, a câmera do modo foto e arrastar o visualizador do carro. O cursor do mapa não é afetado. Aplica-se na hora.",
         "Quanto a visão gira a cada movimento do mouse. Aplica-se na hora.",
         "Com que rapidez as teclas de direção viram o volante e o deixam voltar. Aplica-se na hora.",
         "Um ponteiro no menu de pausa e no mapa: clique para escolher, botão direito para voltar, roda para rolar. Aplica-se na hora.",
         "Tamanho do ponteiro dos menus. Aplica-se na hora.",
         "Com que rapidez o mouse move o cursor do mapa-múndi. Aplica-se na hora."},
    .kbm_modes = {L"Automático", L"Teclado e mouse", L"Controle"},
    .kbm_actions = {"Acelerador", "Freio", "Virar à Esquerda", "Virar à Direita", "Freio de Mão", "Subir Marcha", "Reduzir Marcha", "Embreagem", "Mudar Câmera", "Rebobinar", "Olhar para Trás", "Ativar", "Modo Foto", "Estação Anterior", "Próxima Estação", "Mapa", "Pausa", "Menu: Confirmar", "Menu: Voltar", "Menu: Cima", "Menu: Baixo", "Menu: Esquerda", "Menu: Direita", "Menu: Aba Anterior", "Menu: Próxima Aba", "Menu: Botão X", "Menu: Botão Y", "Menu: Gatilho Esquerdo", "Menu: Gatilho Direito", "Menu: Direcional para Cima", "Menu: Direcional para Baixo", "Menu: Direcional para a Esquerda", "Menu: Direcional para a Direita"},
    .kbm_key_descriptions =
        {"Tecla desta ação de direção. Pressione Enter (ou Esquerda/Direita) na linha e depois a nova tecla; Esc cancela, Backspace ou Delete a remove. Uma tecla usada por outra ação de direção é recusada. Aplica-se na hora.",
         "Tecla desta ação de menu. Pressione Enter (ou Esquerda/Direita) na linha e depois a nova tecla; Esc cancela, Backspace ou Delete a remove. Uma tecla usada por outra ação de menu é recusada. Aplica-se na hora."},
    .kbm_reset = "Redefinir Teclas",
    .kbm_reset_description = "Retorna todas as teclas ao padrão. Pressione Enter ou Esquerda/Direita. Aplica-se na hora.",
    .kbm_labels = {L"Pressione uma tecla...", L"Nenhuma", L"Tecla indisponível", L"Teclado e mouse desligados", L"Falha ao salvar", L"Em uso", L"Redefinir", L"Concluído"}};

static const VideoOptionsText german = {
    .photo_lights = L"Scheinwerfer",
    .player_character = "Spielercharakter",
    .player_character_description = "Wähle das Spielermodell. Standard folgt dem Spiel. Änderungen gelten, sobald das Spielermodell das nächste Mal geladen wird.",
    .open_world_drivatars = "Drivatars in der offenen Welt",
    .open_world_drivatars_description = "Aktiviert Drivatars im freien Fahren. Ein Spielneustart ist erforderlich. Renngegner und Eventbelohnungen bleiben unverändert.",
    .fov_label = "Sichtfeld",
    .skill_counter_shake = "Wackeln des Fähigkeitszählers",
    .skill_counter_shake_description = "Aktiviert das Wackeln und die Größenänderung des Fähigkeitskettenzählers.",
    .speed_zone_vignette = "Blitzerzonen-Vignette",
    .speed_zone_vignette_description = "Aktiviert die zusätzliche Vignette in Blitzerzonen.",
    .skill_point_notification = "Fähigkeitspunkt-Benachrichtigung",
    .skill_point_description = "Zeigt die Benachrichtigung über einen erhaltenen Fähigkeitspunkt und spielt den zugehörigen Dialog ab. Fähigkeitspunkte werden auch bei deaktivierter Option vergeben.",
    .unit_names = {"Leistungseinheiten", "Federeinheiten"},
    .unit_descriptions = {"Wähle die Einheit für die Motorleistung. Standard folgt der Einstellung für Einheiten.", "Wähle die Einheit für die Federhärte. Standard folgt der Einstellung für Einheiten."},
    .master_volume = "Gesamtlautstärke",
    .manufacturer_jump = L"Zum Hersteller springen",
    .event_labels = {L"Drivatar-Anzahl", L"Startreihenfolge", L"Nur Serienfahrzeuge", L"Verkehr"},
    .grid_choices = {L"Zufällig", L"Niedrigster LI zuerst", L"Höchster LI zuerst"},
    .division_label = L"Fahrzeugdivision",
    .garage_label = L"Zur Garage zurückkehren",
    .garage_question = L"Zur Festivalgarage auf der aktuellen Karte zurückkehren?",
    .camera = "Driftkamera",
    .driver = "Fahrer",
    .parameters = {"Empfindlichkeit", "Blickgeschwindigkeit", "Bereich"},
    .descriptions = {"Driftkamera für Verfolger- und Cockpitansicht, nur für das Cockpit "
                     "aktivieren oder ausschalten.",
                     "Das Verhältnis zwischen Fahrzeugwinkel und Kamerawinkel. Erhöhe "
                     "diese "
                     "Einstellung, damit die "
                     "Kamera empfindlicher auf den Fahrzeugwinkel reagiert.",
                     "Die Geschwindigkeit, mit der sich die Kamera aus ihrer "
                     "Ausgangsposition "
                     "in die "
                     "Bewegungsrichtung dreht. Eine niedrigere Einstellung sorgt bei "
                     "Richtungswechseln für "
                     "sanftere Kamerabewegungen.",
                     "Verringere diese Einstellung, um die Kamerabewegung beim maximalen "
                     "Fahrzeugwinkel zu "
                     "begrenzen."},
    .choices = {L"Ein", L"Fahrer", L"Aus"},
    .fov_views = {"Verfolger", "Weiter Verfolger", "Fahrer", "Motorhaube", "Stoßstange"},
    .fov_description = "Stelle das vertikale Sichtfeld der jeweiligen Kamera in Grad ein.",
    .settings_names = {"Echtzeitschatten", "", "Fotomodus-Samples", "", "Minikartenausrichtung",
                       "Autodrive-Filmkamera", "Meisterschaftsdialog überspringen", "Startposition",
                       "Position beim Autowechsel", "Lackierungen in 4K",
                       "Verbessertes Lackierungs-MSAA", "Verbessertes Lackierungs-Sampling",
                       "Alle Events anzeigen", "Autodrive in Events", "Eigene Schaurennen",
                       "Bens Fotomodus-Dialog", "Ashleys Garagen-Dialog",
                       "Wheelspin-Benachrichtigung", "Kontrollpunkt-Soundeffekt"},
    .settings_descriptions =
        {"Flüssige Sonnenschatten steuern. Standard nutzt die Einstellung der aktuellen Übersetzungsschicht. Neustart nötig.",
         "",
         "Fotomodus-Samples wählen. Standard nutzt die Einstellung der aktuellen Übersetzungsschicht. Neustart nötig.",
         "",
         "Wähle, ob sich die Minikarte nach dem Auto oder der Kamera richtet.",
         "Verwendet Filmkameras während Autodrive.",
         "Überspringt den erzwungenen Dialog bei Meisterschaftsrennen mit einem ungeeigneten Auto. Neustart erforderlich.",
         "Wähle die Startposition beim Laden des Spiels. Standard startet wie gewohnt beim Autotreffen, das deiner Position beim Beenden des Spiels am nächsten liegt. Änderungen erfordern einen Spielneustart.",
         "Wo das neue Auto nach einem Fahrzeugwechsel in der offenen Welt stehen soll. Die zweite Option lässt dich genau an der Position des vorherigen Autos.",
         "Nutzt 4K-Lackierungen. Standard folgt der Übersetzungsschicht. Neustart erforderlich.",
         "Nutzt 4x MSAA für Lackierungen. Standard folgt der Übersetzungsschicht. Neustart erforderlich.",
         "Nutzt 2x Sampling für Lackierungen. Standard folgt der Übersetzungsschicht. Neustart erforderlich.",
         "Zeigt alle Rennstartpunkte auf der Karte. Ermöglicht es, jedes Rennen im Spiel zu starten.",
         "Autodrive in Rennen, Roadtrips und anderen Events mit Route erlauben.",
         "Wähle ein eigenes Auto für Schaurennen aus.",
         "Spielt Bens Dialog ab, wenn ein Foto keine erstmals fotografierten Autos enthält.",
         "Spielt Ashleys wiederkehrende Begrüßungen beim Betreten der Garage ab.",
         "Zeigt die Wheelspin-Aufforderung im freien Fahren und spielt den zugehörigen Dialog ab. Nachrichten nach Rennen und Wheelspin-Belohnungen bleiben auch bei deaktivierter Option erhalten.",
         "Spielt beim Überqueren eines Kontrollpunkts einen Soundeffekt ab."},
    .default_choice = L"Standard",
    .minimap_choices = {L"Auto folgen", L"Kamera folgen"},
    .spawn_choices = {L"Einfahrt zum Autotreffen", L"Festivalgelände"},
    .car_position_choices = {L"Nächste Straße", L"Gleiche Position"},
    .keyboard_mouse = "TASTATUR UND MAUS",
    .kbm_names = {"Eingabemodus", "Mausblick", "Mausblick hoch/runter", "Maus-Y-Achse umkehren", "Mausempfindlichkeit", "Lenkgeschwindigkeit (Tastatur)", "Menüzeiger", "Zeigergröße", "Kartenzeiger-Geschwindigkeit"},
    .kbm_descriptions =
        {"Auto wechselt zu dem, was du zuletzt benutzt hast. Tastatur und Maus oder Controller bleiben bei einer Quelle. Gilt sofort.",
         "Beim Fahren dreht die Maus die Kamera; die Ansicht zentriert sich nach kurzer Zeit wieder. Gilt sofort.",
         "In der Stoßstangen-, Motorhauben-, Cockpit- und Fahreransicht neigt das Vor- und Zurückbewegen der Maus die Ansicht nach oben und unten; sie kehrt wie der seitliche Blick zurück. Gilt sofort.",
         "Vertauscht oben und unten für die Maus: Blick nach oben und unten beim Fahren, die Fotomodus-Kamera und das Drehen im Fahrzeugbetrachter. Der Kartenzeiger ist nicht betroffen. Gilt sofort.",
         "Wie weit sich die Ansicht pro Mausbewegung dreht. Gilt sofort.",
         "Wie schnell die Lenktasten das Lenkrad drehen und zurückkehren lassen. Gilt sofort.",
         "Ein Zeiger im Pausenmenü und auf der Karte: Klick wählt aus, Rechtsklick geht zurück, das Mausrad scrollt. Gilt sofort.",
         "Größe des Menüzeigers. Gilt sofort.",
         "Wie schnell die Maus den Zeiger der Weltkarte bewegt. Gilt sofort."},
    .kbm_modes = {L"Auto", L"Tastatur und Maus", L"Controller"},
    .kbm_actions = {"Gas", "Bremse", "Links lenken", "Rechts lenken", "Handbremse", "Hochschalten", "Herunterschalten", "Kupplung", "Kamera wechseln", "Zurückspulen", "Nach hinten schauen", "Aktivieren", "Fotomodus", "Vorheriger Sender", "Nächster Sender", "Karte", "Pause", "Menü: Bestätigen", "Menü: Zurück", "Menü: Hoch", "Menü: Runter", "Menü: Links", "Menü: Rechts", "Menü: Vorheriger Tab", "Menü: Nächster Tab", "Menü: X-Taste", "Menü: Y-Taste", "Menü: Linker Trigger", "Menü: Rechter Trigger", "Menü: Steuerkreuz hoch", "Menü: Steuerkreuz runter", "Menü: Steuerkreuz links", "Menü: Steuerkreuz rechts"},
    .kbm_key_descriptions =
        {"Taste für diese Fahraktion. Drücke Enter (oder Links/Rechts) in der Zeile, dann die neue Taste; Esc bricht ab, Backspace oder Delete löscht sie. Eine Taste, die eine andere Fahraktion nutzt, wird abgelehnt. Gilt sofort.",
         "Taste für diese Menüaktion. Drücke Enter (oder Links/Rechts) in der Zeile, dann die neue Taste; Esc bricht ab, Backspace oder Delete löscht sie. Eine Taste, die eine andere Menüaktion nutzt, wird abgelehnt. Gilt sofort."},
    .kbm_reset = "Tastenbelegung zurücksetzen",
    .kbm_reset_description = "Setzt alle Tastenbelegungen auf den Standard zurück. Drücke Enter oder Links/Rechts. Gilt sofort.",
    .kbm_labels = {L"Taste drücken...", L"Keine", L"Taste nicht verfügbar", L"Tastatur und Maus aus", L"Speichern fehlgeschlagen", L"Belegt", L"Zurücksetzen", L"Erledigt"}};

static const VideoOptionsText french = {
    .photo_lights = L"Phares",
    .player_character = "Personnage du joueur",
    .player_character_description = "Choisissez le modèle du joueur. Par défaut suit le jeu. Les modifications prennent effet au prochain chargement du modèle du joueur.",
    .open_world_drivatars = "Drivatars en monde ouvert",
    .open_world_drivatars_description = "Active les Drivatars en exploration libre. Un redémarrage du jeu est nécessaire. Les adversaires en course et les récompenses des épreuves restent inchangés.",
    .fov_label = "Champ de vision",
    .skill_counter_shake = "Tremblement du compteur de prouesses",
    .skill_counter_shake_description = "Active les tremblements et l'agrandissement du compteur d'enchaînement de prouesses.",
    .speed_zone_vignette = "Vignette des zones de vitesse",
    .speed_zone_vignette_description = "Active la vignette supplémentaire dans les zones de vitesse.",
    .skill_point_notification = "Notification de point de prouesse",
    .skill_point_description = "Affiche la notification de point de prouesse obtenu et joue le dialogue associé. Les points sont toujours attribués lorsque cette option est désactivée.",
    .unit_names = {"Unités de puissance", "Unités des ressorts"},
    .unit_descriptions = {"Choisissez l'unité de puissance du moteur. Par défaut suit le réglage des unités.", "Choisissez l'unité de raideur des ressorts. Par défaut suit le réglage des unités."},
    .master_volume = "Volume général",
    .manufacturer_jump = L"Aller au constructeur",
    .event_labels = {L"Nombre de Drivatars", L"Ordre de départ", L"Voitures de série uniquement",
                     L"Circulation"},
    .grid_choices = {L"Aléatoire", L"IP le plus bas en premier", L"IP le plus élevé en premier"},
    .division_label = L"Division automobile",
    .garage_label = L"Retourner au garage",
    .garage_question = L"Retourner au garage du festival sur la carte actuelle ?",
    .camera = "Caméra de drift",
    .driver = "Pilote",
    .parameters = {"Sensibilité", "Vitesse de rotation", "Amplitude"},
    .descriptions = {"Activez la caméra de drift en poursuite et dans le "
                     "cockpit, uniquement dans le cockpit, ou désactivez-la.",
                     "Le rapport entre l'angle de la voiture et celui de la "
                     "caméra. Augmentez "
                     "ce réglage pour "
                     "rendre la caméra plus sensible à l'angle de la voiture.",
                     "La vitesse à laquelle la caméra quitte sa position "
                     "initiale pour suivre "
                     "la direction du "
                     "déplacement. Réduire ce réglage adoucit les mouvements "
                     "de caméra lors "
                     "des changements de "
                     "direction.",
                     "Réduisez ce réglage pour limiter le mouvement de la "
                     "caméra lorsque la "
                     "voiture atteint son "
                     "angle maximal."},
    .choices = {L"Oui", L"Pilote", L"Non"},
    .fov_views = {"Poursuite", "Poursuite éloignée", "Pilote", "Capot", "Pare-chocs"},
    .fov_description = "Réglez le champ de vision vertical de la caméra concernée en degrés.",
    .settings_names = {"Ombres en temps réel", "", "Échantillons du mode photo", "",
                       "Orientation de la mini-carte", "Caméra cinématique Autodrive",
                       "Ignorer le dialogue de championnat", "Lieu d’apparition",
                       "Position au changement de voiture", "Livrées en résolution 4K",
                       "MSAA amélioré des livrées", "Échantillonnage amélioré des livrées",
                       "Afficher toutes les épreuves", "Autodrive en épreuve",
                       "Épreuves spéciales personnalisées", "Dialogues de Ben en mode Photo",
                       "Dialogues d’Ashley au garage", "Notification de tirage",
                       "Effet sonore des checkpoints"},
    .settings_descriptions =
        {"Réglez les ombres fluides. Par défaut suit le réglage de la couche de traduction actuelle. Redémarrage requis.",
         "",
         "Réglez les échantillons photo. Par défaut suit la couche de traduction actuelle. Redémarrage requis.",
         "",
         "Choisissez si la mini-carte suit la voiture ou la caméra.",
         "Utilise des caméras cinématiques pendant Autodrive.",
         "Ignore le dialogue obligatoire en entrant dans une course de championnat avec une voiture non admissible. Redémarrage requis.",
         "Choisissez le lieu d’apparition au démarrage du jeu. Par défaut, vous apparaissez comme d’habitude au rassemblement le plus proche de votre position à la fermeture du jeu. Redémarrez le jeu pour appliquer les modifications.",
         "L’endroit où placer la nouvelle voiture lors d’un changement de voiture dans le monde ouvert. La seconde option vous laisse exactement à l’emplacement de l’ancienne voiture.",
         "Livrées en 4K. Par défaut suit la couche de traduction. Redémarrage requis.",
         "MSAA 4x des livrées. Par défaut suit la couche de traduction. Redémarrage requis.",
         "Échantillonnage 2x des livrées. Par défaut suit la couche de traduction. Redémarrage requis.",
         "Affiche tous les points de configuration de course sur la carte pour lancer n’importe quelle course du jeu.",
         "Autoriser Autodrive en course, en road trip et dans les épreuves avec un itinéraire.",
         "Choisissez votre propre voiture pour les épreuves spéciales.",
         "Joue le dialogue de Ben lorsqu'une photo ne contient aucune voiture photographiée pour la première fois.",
         "Joue les salutations récurrentes d'Ashley à l'entrée du garage.",
         "Affiche l'invite de tirage en exploration libre et joue le dialogue associé. Les messages après les courses et les récompenses des tirages restent actifs lorsque cette option est désactivée.",
         "Joue l'effet sonore au passage d'un point de contrôle."},
    .default_choice = L"Par défaut",
    .minimap_choices = {L"Suivre la voiture", L"Suivre la caméra"},
    .spawn_choices = {L"Entrée du rassemblement", L"Site du festival"},
    .car_position_choices = {L"Route la plus proche", L"Même emplacement"},
    .keyboard_mouse = "CLAVIER ET SOURIS",
    .kbm_names = {"Mode d'entrée", "Vue à la souris", "Vue haut/bas à la souris", "Inverser l'axe Y de la souris", "Sensibilité de la souris", "Vitesse de direction au clavier", "Pointeur dans les menus", "Taille du curseur", "Vitesse du curseur sur la carte"},
    .kbm_descriptions =
        {"Auto bascule sur le dernier périphérique utilisé. Clavier et souris ou Manette n'en gardent qu'un. S'applique immédiatement.",
         "En conduite, la souris tourne la caméra et la vue se recentre après un instant. S'applique immédiatement.",
         "Dans les vues pare-chocs, capot, cockpit et pilote, avancer et reculer la souris incline la vue vers le haut et le bas ; elle revient comme la vue latérale. S'applique immédiatement.",
         "Inverse le haut et le bas pour la souris : regarder en haut et en bas en conduite, la caméra du mode photo et la rotation dans la visionneuse de voiture. Le curseur de la carte n'est pas concerné. S'applique immédiatement.",
         "L'amplitude de rotation de la vue par mouvement de souris. S'applique immédiatement.",
         "La vitesse à laquelle les touches de direction tournent le volant et le laissent revenir. S'applique immédiatement.",
         "Un pointeur dans le menu pause et sur la carte : clic pour choisir, clic droit pour revenir, molette pour défiler. S'applique immédiatement.",
         "Taille du pointeur des menus. S'applique immédiatement.",
         "La vitesse à laquelle la souris déplace le curseur de la carte du monde. S'applique immédiatement."},
    .kbm_modes = {L"Auto", L"Clavier et souris", L"Manette"},
    .kbm_actions = {"Accélérer", "Freiner", "Tourner à gauche", "Tourner à droite", "Frein à main", "Monter un rapport", "Descendre un rapport", "Embrayage", "Changer de caméra", "Rembobiner", "Regarder derrière", "Activer", "Mode photo", "Station précédente", "Station suivante", "Carte", "Pause", "Menu : valider", "Menu : retour", "Menu : haut", "Menu : bas", "Menu : gauche", "Menu : droite", "Menu : onglet précédent", "Menu : onglet suivant", "Menu : bouton X", "Menu : bouton Y", "Menu : gâchette gauche", "Menu : gâchette droite", "Menu : croix haut", "Menu : croix bas", "Menu : croix gauche", "Menu : croix droite"},
    .kbm_key_descriptions =
        {"Touche de cette action de conduite. Appuyez sur Enter (ou Gauche/Droite) sur la ligne, puis sur la nouvelle touche ; Esc annule, Backspace ou Delete l'efface. Une touche utilisée par une autre action de conduite est refusée. S'applique immédiatement.",
         "Touche de cette action de menu. Appuyez sur Enter (ou Gauche/Droite) sur la ligne, puis sur la nouvelle touche ; Esc annule, Backspace ou Delete l'efface. Une touche utilisée par une autre action de menu est refusée. S'applique immédiatement."},
    .kbm_reset = "Réinitialiser les touches",
    .kbm_reset_description = "Rétablit toutes les touches par défaut. Appuyez sur Enter ou Gauche/Droite. S'applique immédiatement.",
    .kbm_labels = {L"Appuyez sur une touche...", L"Aucune", L"Touche indisponible", L"Clavier et souris désactivés", L"Échec de l'enregistrement", L"Utilisée", L"Réinitialiser", L"Terminé"}};

static const VideoOptionsText spanish = {
    .photo_lights = L"Faros",
    .player_character = "Personaje del jugador",
    .player_character_description = "Elige el modelo del jugador. Predeterminado sigue al juego. Los cambios se aplican la próxima vez que se cargue el modelo del jugador.",
    .open_world_drivatars = "Drivatars en el mundo abierto",
    .open_world_drivatars_description = "Activa los Drivatars en el modo libre. Es necesario reiniciar el juego. No afecta a los rivales de las carreras ni a las recompensas de los eventos.",
    .fov_label = "Campo de visión",
    .skill_counter_shake = "Vibración del contador de habilidades",
    .skill_counter_shake_description = "Activa la vibración y el aumento de tamaño del contador de cadenas de habilidades.",
    .speed_zone_vignette = "Viñeta en zonas de velocidad",
    .speed_zone_vignette_description = "Activa la viñeta adicional durante las zonas de velocidad.",
    .skill_point_notification = "Notificación de punto de habilidad",
    .skill_point_description = "Muestra la notificación de punto de habilidad obtenido y reproduce su diálogo. Los puntos se siguen otorgando aunque se desactive esta opción.",
    .unit_names = {"Unidades de potencia", "Unidades de los muelles"},
    .unit_descriptions = {"Elige la unidad de potencia del motor. Predeterminado sigue el ajuste de unidades.", "Elige la unidad de rigidez de los muelles. Predeterminado sigue el ajuste de unidades."},
    .master_volume = "Volumen general",
    .manufacturer_jump = L"Ir al fabricante",
    .event_labels = {L"Número de Drivatars", L"Orden de salida", L"Solo coches de serie",
                     L"Tráfico"},
    .grid_choices = {L"Aleatorio", L"Menor PI primero", L"Mayor PI primero"},
    .division_label = L"División de coches",
    .garage_label = L"Volver al garaje",
    .garage_question = L"¿Volver al garaje del festival del mapa actual?",
    .camera = "Cámara de derrape",
    .driver = "Piloto",
    .parameters = {"Sensibilidad", "Velocidad de giro", "Rango"},
    .descriptions = {"Activa la cámara de derrape en persecución y cabina, solo en la "
                     "cabina, o desactívala.",
                     "La proporción entre el ángulo del coche y el de la cámara. Aumenta "
                     "este "
                     "ajuste para que la "
                     "cámara sea más sensible al ángulo del coche.",
                     "La velocidad a la que la cámara se mueve desde su posición original "
                     "hacia la dirección de "
                     "desplazamiento. Reducir este ajuste suaviza el movimiento de la "
                     "cámara "
                     "durante los cambios "
                     "de dirección.",
                     "Reduce este ajuste para limitar el movimiento de la cámara cuando el "
                     "coche alcance su ángulo "
                     "máximo."},
    .choices = {L"Sí", L"Piloto", L"No"},
    .fov_views = {"Seguimiento", "Seguimiento lejano", "Piloto", "Capó", "Parachoques"},
    .fov_description = "Establece el campo de visión vertical de la cámara correspondiente en "
                       "grados.",
    .settings_names = {"Sombras en tiempo real", "", "Muestras del modo Foto", "",
                       "Orientación del minimapa", "Cámara cinemática de Autodrive",
                       "Omitir diálogo de campeonato", "Ubicación inicial",
                       "Posición al cambiar de coche", "Diseños en resolución 4K",
                       "MSAA mejorado de diseños", "Muestreo mejorado de diseños",
                       "Mostrar todos los eventos", "Autodrive en eventos",
                       "Exhibiciones personalizadas", "Diálogos de Ben en el modo Foto",
                       "Diálogos de Ashley en el garaje", "Notificación de ruleta",
                       "Sonido de los puntos de control"},
    .settings_descriptions =
        {"Controla las sombras suaves. Predeterminado usa el ajuste de la capa de traducción actual. Reinicia el juego.",
         "",
         "Ajusta las muestras del modo foto. Predeterminado usa la capa de traducción actual. Reinicia el juego.",
         "",
         "Elige si el minimapa sigue al coche o a la cámara.",
         "Usa cámaras cinemáticas durante Autodrive.",
         "Omite el diálogo obligatorio al entrar en una carrera de campeonato con un coche no apto. Requiere reiniciar.",
         "Elige dónde aparecer al iniciar el juego. Predeterminado te sitúa, como siempre, en la reunión de coches más cercana a tu posición al cerrar el juego. Reinicia el juego para aplicar los cambios.",
         "Dónde debe estar el nuevo coche al cambiar de coche en el mundo abierto. La segunda opción te deja exactamente donde estaba el coche anterior.",
         "Usa diseños en 4K. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Usa MSAA 4x en diseños. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Usa muestreo 2x en diseños. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Muestra todos los puntos de configuración de carreras en el mapa. Permite iniciar cualquier carrera del juego.",
         "Permite Autodrive en carreras, viajes y otros eventos con ruta.",
         "Elige tu propio coche para participar en las exhibiciones.",
         "Reproduce el diálogo de Ben al tomar una foto sin coches fotografiados por primera vez.",
         "Reproduce los saludos habituales de Ashley al entrar en el garaje.",
         "Muestra el aviso de sorteo en el modo libre y reproduce su diálogo. Los mensajes posteriores a las carreras y las recompensas de los sorteos siguen activos aunque se desactive esta opción.",
         "Reproduce el efecto de sonido al cruzar un punto de control."},
    .default_choice = L"Predeterminado",
    .minimap_choices = {L"Seguir coche", L"Seguir cámara"},
    .spawn_choices = {L"Entrada de la reunión de coches", L"Recinto del festival"},
    .car_position_choices = {L"Carretera más cercana", L"Misma ubicación"},
    .keyboard_mouse = "TECLADO Y RATÓN",
    .kbm_names = {"Modo de entrada", "Vista con el ratón", "Vista arriba/abajo con el ratón", "Invertir eje Y del ratón", "Sensibilidad del ratón", "Velocidad de giro con teclado", "Puntero en los menús", "Tamaño del cursor", "Velocidad del cursor en el mapa"},
    .kbm_descriptions =
        {"Automático cambia a lo último que hayas usado. Teclado y ratón o Mando mantienen una sola fuente. Se aplica al instante.",
         "Al conducir, el ratón gira la cámara y la vista se recentra tras un momento. Se aplica al instante.",
         "En las vistas de parachoques, capó, cabina y piloto, mover el ratón hacia delante y atrás inclina la vista arriba y abajo; vuelve como la vista lateral. Se aplica al instante.",
         "Invierte arriba y abajo para el ratón: mirar arriba y abajo al conducir, la cámara del modo foto y girar el coche en el visor. No afecta al cursor del mapa. Se aplica al instante.",
         "Cuánto gira la vista por cada movimiento del ratón. Se aplica al instante.",
         "Lo rápido que las teclas de dirección giran el volante y lo dejan volver. Se aplica al instante.",
         "Un puntero en el menú de pausa y en el mapa: clic para elegir, clic derecho para volver, rueda para desplazarse. Se aplica al instante.",
         "Tamaño del puntero de los menús. Se aplica al instante.",
         "Lo rápido que el ratón mueve el cursor del mapa del mundo. Se aplica al instante."},
    .kbm_modes = {L"Automático", L"Teclado y ratón", L"Mando"},
    .kbm_actions = {"Acelerador", "Freno", "Girar a la izquierda", "Girar a la derecha", "Freno de mano", "Subir marcha", "Bajar marcha", "Embrague", "Cambiar cámara", "Rebobinar", "Mirar atrás", "Activar", "Modo foto", "Emisora anterior", "Emisora siguiente", "Mapa", "Pausa", "Menú: confirmar", "Menú: atrás", "Menú: arriba", "Menú: abajo", "Menú: izquierda", "Menú: derecha", "Menú: pestaña anterior", "Menú: pestaña siguiente", "Menú: botón X", "Menú: botón Y", "Menú: gatillo izquierdo", "Menú: gatillo derecho", "Menú: cruceta arriba", "Menú: cruceta abajo", "Menú: cruceta izquierda", "Menú: cruceta derecha"},
    .kbm_key_descriptions =
        {"Tecla de esta acción de conducción. Pulsa Enter (o Izquierda/Derecha) en la fila y luego la nueva tecla; Esc cancela, Backspace o Delete la borra. Se rechaza una tecla que ya usa otra acción de conducción. Se aplica al instante.",
         "Tecla de esta acción de menú. Pulsa Enter (o Izquierda/Derecha) en la fila y luego la nueva tecla; Esc cancela, Backspace o Delete la borra. Se rechaza una tecla que ya usa otra acción de menú. Se aplica al instante."},
    .kbm_reset = "Restablecer teclas",
    .kbm_reset_description = "Devuelve todas las teclas a sus valores predeterminados. Pulsa Enter o Izquierda/Derecha. Se aplica al instante.",
    .kbm_labels = {L"Pulsa una tecla...", L"Ninguna", L"Tecla no disponible", L"Teclado y ratón desactivados", L"Error al guardar", L"En uso", L"Restablecer", L"Hecho"}};

static const VideoOptionsText mexican = {
    .photo_lights = L"Faros",
    .player_character = "Personaje del jugador",
    .player_character_description = "Elige el modelo del jugador. Predeterminado sigue al juego. Los cambios se aplican la próxima vez que se cargue el modelo del jugador.",
    .open_world_drivatars = "Drivatars en el mundo abierto",
    .open_world_drivatars_description = "Activa los Drivatars en el modo libre. Es necesario reiniciar el juego. No afecta a los rivales de las carreras ni a las recompensas de los eventos.",
    .fov_label = "Campo de visión",
    .skill_counter_shake = "Vibración del contador de habilidades",
    .skill_counter_shake_description = "Activa la vibración y el aumento de tamaño del contador de cadenas de habilidades.",
    .speed_zone_vignette = "Viñeta en zonas de velocidad",
    .speed_zone_vignette_description = "Activa la viñeta adicional durante las zonas de velocidad.",
    .skill_point_notification = "Notificación de punto de habilidad",
    .skill_point_description = "Muestra la notificación de punto de habilidad obtenido y reproduce su diálogo. Los puntos se siguen otorgando aunque se desactive esta opción.",
    .unit_names = {"Unidades de potencia", "Unidades de los resortes"},
    .unit_descriptions = {"Elige la unidad de potencia del motor. Predeterminado sigue la configuración de unidades.", "Elige la unidad de rigidez de los resortes. Predeterminado sigue la configuración de unidades."},
    .master_volume = "Volumen general",
    .manufacturer_jump = L"Ir al fabricante",
    .event_labels = {L"Número de Drivatars", L"Orden de salida", L"Solo autos de serie",
                     L"Tráfico"},
    .grid_choices = {L"Aleatorio", L"Menor PI primero", L"Mayor PI primero"},
    .division_label = L"División de autos",
    .garage_label = L"Volver al garage",
    .garage_question = L"¿Volver al garage del festival del mapa actual?",
    .camera = "Cámara de derrape",
    .driver = "Piloto",
    .parameters = {"Sensibilidad", "Velocidad de giro", "Rango"},
    .descriptions = {"Activa la cámara de derrape en persecución y cabina, solo en la "
                     "cabina, o desactívala.",
                     "La proporción entre el ángulo del auto y el de la cámara. Aumenta "
                     "este "
                     "ajuste para que la "
                     "cámara sea más sensible al ángulo del auto.",
                     "La velocidad a la que la cámara se mueve desde su posición original "
                     "hacia la dirección de "
                     "desplazamiento. Reducir este ajuste suaviza el movimiento de la "
                     "cámara "
                     "durante los cambios "
                     "de dirección.",
                     "Reduce este ajuste para limitar el movimiento de la cámara cuando el "
                     "auto alcance su ángulo "
                     "máximo."},
    .choices = {L"Sí", L"Piloto", L"No"},
    .fov_views = {"Seguimiento", "Seguimiento lejano", "Piloto", "Capó", "Parachoques"},
    .fov_description = "Establece el campo de visión vertical de la cámara correspondiente en "
                       "grados.",
    .settings_names = {"Sombras en tiempo real", "", "Muestras del modo Foto", "",
                       "Orientación del minimapa", "Cámara cinemática de Autodrive",
                       "Omitir diálogo de campeonato", "Ubicación inicial",
                       "Posición al cambiar de auto", "Diseños en resolución 4K",
                       "MSAA mejorado de diseños", "Muestreo mejorado de diseños",
                       "Mostrar todos los eventos", "Autodrive en eventos",
                       "Exhibiciones personalizadas", "Diálogos de Ben en el modo Foto",
                       "Diálogos de Ashley en el garaje", "Notificación de ruleta",
                       "Sonido de los puntos de control"},
    .settings_descriptions =
        {"Controla las sombras suaves. Predeterminado usa el ajuste de la capa de traducción actual. Reinicia el juego.",
         "",
         "Ajusta las muestras del modo foto. Predeterminado usa la capa de traducción actual. Reinicia el juego.",
         "",
         "Elige si el minimapa sigue al auto o a la cámara.",
         "Usa cámaras cinemáticas durante Autodrive.",
         "Omite el diálogo obligatorio al entrar en una carrera de campeonato con un auto no apto. Requiere reiniciar.",
         "Elige dónde aparecer al iniciar el juego. Predeterminado te sitúa, como siempre, en la reunión de autos más cercana a tu posición al cerrar el juego. Reinicia el juego para aplicar los cambios.",
         "Dónde debe estar el nuevo auto al cambiar de auto en el mundo abierto. La segunda opción te deja exactamente donde estaba el auto anterior.",
         "Usa diseños en 4K. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Usa MSAA 4x en diseños. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Usa muestreo 2x en diseños. Predeterminado sigue la capa de traducción. Requiere reiniciar.",
         "Muestra todos los puntos de configuración de carreras en el mapa. Permite iniciar cualquier carrera del juego.",
         "Permite Autodrive en carreras, viajes y otros eventos con ruta.",
         "Elige tu propio auto para participar en las exhibiciones.",
         "Reproduce el diálogo de Ben al tomar una foto sin autos fotografiados por primera vez.",
         "Reproduce los saludos habituales de Ashley al entrar al garaje.",
         "Muestra el aviso de sorteo en el modo libre y reproduce su diálogo. Los mensajes posteriores a las carreras y las recompensas de los sorteos siguen activos aunque se desactive esta opción.",
         "Reproduce el efecto de sonido al cruzar un punto de control."},
    .default_choice = L"Predeterminado",
    .minimap_choices = {L"Seguir auto", L"Seguir cámara"},
    .spawn_choices = {L"Entrada de la reunión de autos", L"Recinto del festival"},
    .car_position_choices = {L"Carretera más cercana", L"Misma ubicación"},
    .keyboard_mouse = "TECLADO Y MOUSE",
    .kbm_names = {"Modo de entrada", "Vista con el mouse", "Vista arriba/abajo con el mouse", "Invertir eje Y del mouse", "Sensibilidad del mouse", "Velocidad de giro con teclado", "Puntero en los menús", "Tamaño del cursor", "Velocidad del cursor en el mapa"},
    .kbm_descriptions =
        {"Automático cambia a lo último que hayas usado. Teclado y mouse o Control mantienen una sola fuente. Se aplica al instante.",
         "Al conducir, el mouse gira la cámara y la vista se recentra tras un momento. Se aplica al instante.",
         "En las vistas de parachoques, capó, cabina y piloto, mover el mouse hacia delante y atrás inclina la vista arriba y abajo; vuelve como la vista lateral. Se aplica al instante.",
         "Invierte arriba y abajo para el mouse: mirar arriba y abajo al conducir, la cámara del modo foto y girar el auto en el visor. No afecta al cursor del mapa. Se aplica al instante.",
         "Cuánto gira la vista por cada movimiento del mouse. Se aplica al instante.",
         "Lo rápido que las teclas de dirección giran el volante y lo dejan volver. Se aplica al instante.",
         "Un puntero en el menú de pausa y en el mapa: clic para elegir, clic derecho para volver, rueda para desplazarse. Se aplica al instante.",
         "Tamaño del puntero de los menús. Se aplica al instante.",
         "Lo rápido que el mouse mueve el cursor del mapa del mundo. Se aplica al instante."},
    .kbm_modes = {L"Automático", L"Teclado y mouse", L"Control"},
    .kbm_actions = {"Acelerador", "Freno", "Girar a la izquierda", "Girar a la derecha", "Freno de mano", "Subir marcha", "Bajar marcha", "Embrague", "Cambiar cámara", "Rebobinar", "Mirar atrás", "Activar", "Modo foto", "Estación anterior", "Estación siguiente", "Mapa", "Pausa", "Menú: confirmar", "Menú: atrás", "Menú: arriba", "Menú: abajo", "Menú: izquierda", "Menú: derecha", "Menú: pestaña anterior", "Menú: pestaña siguiente", "Menú: botón X", "Menú: botón Y", "Menú: gatillo izquierdo", "Menú: gatillo derecho", "Menú: cruceta arriba", "Menú: cruceta abajo", "Menú: cruceta izquierda", "Menú: cruceta derecha"},
    .kbm_key_descriptions =
        {"Tecla de esta acción de conducción. Presiona Enter (o Izquierda/Derecha) en la fila y luego la nueva tecla; Esc cancela, Backspace o Delete la borra. Se rechaza una tecla que ya usa otra acción de conducción. Se aplica al instante.",
         "Tecla de esta acción de menú. Presiona Enter (o Izquierda/Derecha) en la fila y luego la nueva tecla; Esc cancela, Backspace o Delete la borra. Se rechaza una tecla que ya usa otra acción de menú. Se aplica al instante."},
    .kbm_reset = "Restablecer teclas",
    .kbm_reset_description = "Devuelve todas las teclas a sus valores predeterminados. Presiona Enter o Izquierda/Derecha. Se aplica al instante.",
    .kbm_labels = {L"Presiona una tecla...", L"Ninguna", L"Tecla no disponible", L"Teclado y mouse desactivados", L"Error al guardar", L"En uso", L"Restablecer", L"Listo"}};

static const VideoOptionsText italian = {
    .photo_lights = L"Fari",
    .player_character = "Personaggio del giocatore",
    .player_character_description = "Scegli il modello del giocatore. Predefinito segue il gioco. Le modifiche si applicano al successivo caricamento del modello del giocatore.",
    .open_world_drivatars = "Drivatar nel mondo aperto",
    .open_world_drivatars_description = "Attiva i Drivatar nella guida libera. È necessario riavviare il gioco. Non modifica gli avversari delle gare né le ricompense degli eventi.",
    .fov_label = "Campo visivo",
    .skill_counter_shake = "Oscillazione del contatore abilità",
    .skill_counter_shake_description = "Attiva l'oscillazione e l'ingrandimento del contatore delle catene di abilità.",
    .speed_zone_vignette = "Vignettatura zone di velocità",
    .speed_zone_vignette_description = "Attiva la vignettatura aggiuntiva nelle zone di velocità.",
    .skill_point_notification = "Notifica punto abilità",
    .skill_point_description = "Mostra la notifica del punto abilità ottenuto e riproduce il dialogo associato. I punti vengono assegnati anche quando l'opzione è disattivata.",
    .unit_names = {"Unità di potenza", "Unità delle molle"},
    .unit_descriptions = {"Scegli l'unità di potenza del motore. Predefinito segue l'impostazione delle unità.", "Scegli l'unità di rigidità delle molle. Predefinito segue l'impostazione delle unità."},
    .master_volume = "Volume generale",
    .manufacturer_jump = L"Vai al costruttore",
    .event_labels = {L"Numero di Drivatar", L"Ordine di partenza", L"Solo auto di serie",
                     L"Traffico"},
    .grid_choices = {L"Casuale", L"Prima IP più basso", L"Prima IP più alto"},
    .division_label = L"Divisione auto",
    .garage_label = L"Torna al garage",
    .garage_question = L"Tornare al garage del festival sulla mappa attuale?",
    .camera = "Telecamera di derapata",
    .driver = "Pilota",
    .parameters = {"Sensibilità", "Velocità di rotazione", "Ampiezza"},
    .descriptions = {"Attiva la telecamera di derapata in inseguimento e nell’abitacolo, "
                     "solo nell’abitacolo, oppure disattivala.",
                     "Il rapporto tra l'angolo dell'auto e quello della telecamera. "
                     "Aumenta "
                     "questa impostazione "
                     "per rendere la telecamera più sensibile all'angolo dell'auto.",
                     "La velocità con cui la telecamera si sposta dalla posizione "
                     "originale "
                     "verso la direzione di "
                     "movimento. Ridurre questa impostazione rende più fluidi i movimenti "
                     "della telecamera durante "
                     "i cambi di direzione.",
                     "Riduci questa impostazione per limitare il movimento della "
                     "telecamera "
                     "quando l'auto "
                     "raggiunge l'angolo massimo."},
    .choices = {L"Sì", L"Pilota", L"No"},
    .fov_views = {"Inseguimento", "Inseguimento distante", "Pilota", "Cofano", "Paraurti"},
    .fov_description = "Imposta il campo visivo verticale della telecamera "
                       "selezionata in gradi.",
    .settings_names = {"Ombre in tempo reale", "", "Campioni modalità Foto", "",
                       "Orientamento minimappa", "Telecamera cinematografica Autodrive",
                       "Salta dialogo campionato", "Posizione iniziale", "Posizione al cambio auto",
                       "Livree in risoluzione 4K", "MSAA migliorato delle livree",
                       "Campionamento migliorato delle livree", "Mostra tutti gli eventi",
                       "Autodrive negli eventi", "Eventi esibizione personalizzati",
                       "Dialoghi di Ben in modalità Foto", "Dialoghi di Ashley nel garage",
                       "Notifica della ruota", "Effetto sonoro dei checkpoint"},
    .settings_descriptions =
        {"Regola le ombre fluide. Predefinito usa l'impostazione del livello di traduzione attuale. Riavvio richiesto.",
         "",
         "Regola i campioni foto. Predefinito usa l'impostazione del livello di traduzione attuale. Riavvio richiesto.",
         "",
         "Scegli se la minimappa segue l’auto o la telecamera.",
         "Usa telecamere cinematografiche durante Autodrive.",
         "Salta il dialogo obbligatorio entrando in una gara di campionato con un’auto non idonea. Riavvio richiesto.",
         "Scegli la posizione iniziale all’avvio del gioco. Predefinito ti colloca come al solito al raduno più vicino alla tua posizione alla chiusura del gioco. Riavvia il gioco per applicare le modifiche.",
         "Dove posizionare la nuova auto quando cambi auto nel mondo aperto. La seconda opzione ti lascia esattamente nella posizione dell’auto precedente.",
         "Usa livree in 4K. Predefinito segue il livello di traduzione. Riavvio necessario.",
         "Usa MSAA 4x per le livree. Predefinito segue il livello di traduzione. Riavvio necessario.",
         "Usa campionamento 2x. Predefinito segue il livello di traduzione. Riavvio necessario.",
         "Mostra tutti i punti di configurazione delle gare sulla mappa. Permette di avviare qualsiasi gara del gioco.",
         "Consenti Autodrive in gare, viaggi e altri eventi con un percorso.",
         "Scegli la tua auto per partecipare agli eventi esibizione.",
         "Riproduce il dialogo di Ben quando una foto non contiene auto fotografate per la prima volta.",
         "Riproduce i saluti ricorrenti di Ashley quando entri nel garage.",
         "Mostra l'invito al giro della ruota nella guida libera e riproduce il dialogo associato. I messaggi dopo le gare e i premi della ruota restano attivi anche quando l'opzione è disattivata.",
         "Riproduce l'effetto sonoro quando attraversi un checkpoint."},
    .default_choice = L"Predefinito",
    .minimap_choices = {L"Segui auto", L"Segui telecamera"},
    .spawn_choices = {L"Ingresso del raduno", L"Area del festival"},
    .car_position_choices = {L"Strada più vicina", L"Stessa posizione"},
    .keyboard_mouse = "TASTIERA E MOUSE",
    .kbm_names = {"Modalità di input", "Visuale con il mouse", "Visuale su/giù con il mouse", "Inverti asse Y del mouse", "Sensibilità del mouse", "Velocità di sterzata da tastiera", "Puntatore nei menu", "Dimensione del cursore", "Velocità del cursore sulla mappa"},
    .kbm_descriptions =
        {"Auto passa all'ultimo dispositivo usato. Tastiera e mouse o Controller mantengono una sola sorgente. Si applica subito.",
         "Durante la guida il mouse ruota la telecamera e la visuale si ricentra dopo un attimo. Si applica subito.",
         "Nelle visuali paraurti, cofano, abitacolo e pilota, muovere il mouse avanti e indietro inclina la visuale in alto e in basso; torna come la visuale laterale. Si applica subito.",
         "Scambia alto e basso per il mouse: guardare in alto e in basso durante la guida, la telecamera della modalità foto e la rotazione nel visualizzatore auto. Il cursore della mappa non cambia. Si applica subito.",
         "Quanto ruota la visuale per ogni movimento del mouse. Si applica subito.",
         "Quanto velocemente i tasti di sterzata girano il volante e lo lasciano tornare. Si applica subito.",
         "Un puntatore nel menu di pausa e sulla mappa: clic per scegliere, clic destro per tornare indietro, rotellina per scorrere. Si applica subito.",
         "Dimensione del puntatore dei menu. Si applica subito.",
         "Quanto velocemente il mouse sposta il cursore della mappa del mondo. Si applica subito."},
    .kbm_modes = {L"Auto", L"Tastiera e mouse", L"Controller"},
    .kbm_actions = {"Acceleratore", "Freno", "Sterza a sinistra", "Sterza a destra", "Freno a mano", "Marcia su", "Marcia giù", "Frizione", "Cambia telecamera", "Riavvolgi", "Guarda dietro", "Attiva", "Modalità foto", "Stazione precedente", "Stazione successiva", "Mappa", "Pausa", "Menu: conferma", "Menu: indietro", "Menu: su", "Menu: giù", "Menu: sinistra", "Menu: destra", "Menu: scheda precedente", "Menu: scheda successiva", "Menu: tasto X", "Menu: tasto Y", "Menu: grilletto sinistro", "Menu: grilletto destro", "Menu: croce direzionale su", "Menu: croce direzionale giù", "Menu: croce direzionale sinistra", "Menu: croce direzionale destra"},
    .kbm_key_descriptions =
        {"Tasto per questa azione di guida. Premi Enter (o Sinistra/Destra) sulla riga, poi il nuovo tasto; Esc annulla, Backspace o Delete lo cancella. Un tasto usato da un'altra azione di guida viene rifiutato. Si applica subito.",
         "Tasto per questa azione del menu. Premi Enter (o Sinistra/Destra) sulla riga, poi il nuovo tasto; Esc annulla, Backspace o Delete lo cancella. Un tasto usato da un'altra azione del menu viene rifiutato. Si applica subito."},
    .kbm_reset = "Ripristina tasti",
    .kbm_reset_description = "Riporta tutti i tasti ai valori predefiniti. Premi Enter o Sinistra/Destra. Si applica subito.",
    .kbm_labels = {L"Premi un tasto...", L"Nessuno", L"Tasto non disponibile", L"Tastiera e mouse disattivati", L"Salvataggio non riuscito", L"In uso", L"Ripristina", L"Fatto"}};

static const VideoOptionsText japanese = {
    .photo_lights = L"ヘッドライト",
    .player_character = "プレイヤーキャラクター",
    .player_character_description = "プレイヤーのモデルを選択します。「デフォルト」はゲームの設定に従います。変更はプレイヤーモデルの次回読み込み時に適用されます。",
    .open_world_drivatars = "オープンワールドの Drivatar",
    .open_world_drivatars_description = "フリー走行で Drivatar を出現させます。適用にはゲームの再起動が必要です。レースの対戦相手やイベント報酬には影響しません。",
    .fov_label = "視野角",
    .skill_counter_shake = "スキルカウンターの揺れ",
    .skill_counter_shake_description = "スキルチェーンカウンターの揺れと拡大を有効にします。",
    .speed_zone_vignette = "スピードゾーンのビネット",
    .speed_zone_vignette_description = "スピードゾーンで追加のビネット効果を有効にします。",
    .skill_point_notification = "スキルポイント獲得通知",
    .skill_point_description = "スキルポイントの獲得通知を表示し、関連するセリフを再生します。無効でもスキルポイントは獲得できます。",
    .unit_names = {"パワーの単位", "スプリングの単位"},
    .unit_descriptions = {"エンジン出力の表示単位を選択します。「デフォルト」は単位設定に従います。", "スプリングの硬さの表示単位を選択します。「デフォルト」は単位設定に従います。"},
    .master_volume = "マスター音量",
    .manufacturer_jump = L"メーカーに移動",
    .event_labels = {L"Drivatar 数", L"スタート順", L"ノーマル車両のみ", L"一般車両"},
    .grid_choices = {L"ランダム", L"PI の低い順", L"PI の高い順"},
    .division_label = L"カー ディビジョン",
    .garage_label = L"ガレージに戻る",
    .garage_question = L"現在のマップのフェスティバル ガレージに戻りますか？",
    .camera = "ドリフトカメラ",
    .driver = "ドライバー",
    .parameters = {"感度", "視点移動速度", "可動範囲"},
    .descriptions = {"ドリフトカメラを追従・コックピット視点で有効にする、コッ"
                     "クピット視点のみで有効にする、または無効にします。",
                     "車の角度とカメラの角度の比率です。この設定を上げると、車"
                     "の角度に対するカ"
                     "メラの感度が高くなり"
                     "ます。",
                     "カメラが元の位置から車の移動方向へ向く速度です。この設定"
                     "を下げると、方向"
                     "転換時のカメラの動き"
                     "が滑らかになります。",
                     "この設定を下げると、車が最大角度に達したときのカメラの動"
                     "きを制限できます"
                     "。"},
    .choices = {L"ON", L"ドライバー", L"OFF"},
    .fov_views = {"チェイス", "遠距離チェイス", "ドライバー", "ボンネット", "バンパー"},
    .fov_description = "選択したカメラの垂直視野角を度単位で設定します。",
    .settings_names = {"リアルタイムの影", "", "フォトモードのサンプル数", "", "ミニマップの向き",
                       "Autodriveのシネマティックカメラ", "チャンピオンシップの会話をスキップ",
                       "開始地点", "車の変更時の位置", "4K解像度のデザイン", "デザインの強化MSAA",
                       "デザインの強化サンプリング", "すべてのイベントを表示",
                       "イベント中のAutodrive", "カスタム ショーケース", "フォトモードのベンの会話",
                       "ガレージのアシュリーの会話", "ホイールスピン通知",
                       "チェックポイント効果音"},
    .settings_descriptions =
        {"太陽の影の滑らかさを設定します。既定では現在の変換レイヤーの設定を使用します。再起動が必要です。",
         "",
         "フォトモードのサンプル数を設定します。既定では現在の変換レイヤーの設定を使用します。再起動が必要です。",
         "",
         "ミニマップの向きを車またはカメラに合わせます。",
         "Autodrive中にシネマティックカメラを使用します。",
         "参加条件を満たさない車でチャンピオンシップに入る際の強制会話をスキップします。再起動が必要です。",
         "ゲーム起動時の開始地点を選択します。デフォルトでは、通常どおりゲーム終了時の位置に最も近いカーミーティングから開始します。変更を適用するにはゲームの再起動が必要です。",
         "オープンワールドで車を変更したときの、新しい車の位置を設定します。後者を選ぶと、前の車があった場所にそのまま留まります。",
         "デザインを4Kにします。「デフォルト」は変換レイヤーの設定を使用。再起動が必要です。",
         "デザインに4x MSAAを使用。「デフォルト」は変換レイヤーの設定を使用。再起動が必要です。",
         "デザインに2xサンプリングを使用。「デフォルト」は変換レイヤーの設定を使用。再起動が必要です。",
         "マップ上にすべてのレース設定ポイントを表示します。ゲーム内の任意のレースを開始できます。",
         "レース、ロードトリップなどルートのあるイベントでAutodriveを許可します。",
         "ショーケースイベントに自分のガレージのクルマで参加できます。",
         "初めて撮影する車が写っていない場合に、ベンのセリフを再生します。",
         "ガレージに入る際に、アシュリーのおなじみの挨拶を再生します。",
         "フリー走行中のホイールスピン案内を表示し、関連するセリフを再生します。無効でもレース後のメッセージとスピン報酬は有効です。",
         "チェックポイント通過時の効果音を再生します。"},
    .default_choice = L"デフォルト",
    .minimap_choices = {L"車に合わせる", L"カメラに合わせる"},
    .spawn_choices = {L"カーミーティング入口", L"フェスティバル会場"},
    .car_position_choices = {L"最寄りの道路", L"同じ位置"},
    .keyboard_mouse = "キーボードとマウス",
    .kbm_names = {"入力モード", "マウス視点", "マウス視点（上下）", "マウスY軸反転", "マウス感度", "キーボードのステアリング速度", "メニューポインター", "カーソルサイズ", "マップカーソル速度"},
    .kbm_descriptions =
        {"自動では最後に使った入力に切り替わります。キーボードとマウス、またはコントローラーでは一方に固定されます。すぐに適用されます。",
         "運転中、マウスでカメラを回します。少し経つと視点は中央に戻ります。すぐに適用されます。",
         "バンパー、ボンネット、コックピット、ドライバー視点で、マウスを前後に動かすと視点が上下に傾きます。左右の視点と同様に元に戻ります。すぐに適用されます。",
         "マウスの上下を反転します：運転中の上下の視点、フォトモードのカメラ、車両ビューアーでのドラッグ。マップカーソルには影響しません。すぐに適用されます。",
         "マウスの動きに対して視点が回る量です。すぐに適用されます。",
         "ステアリングキーでハンドルを切る速さと戻る速さです。すぐに適用されます。",
         "ポーズメニューとマップのポインター：クリックで選択、右クリックで戻る、ホイールでスクロールします。すぐに適用されます。",
         "メニューポインターの大きさです。すぐに適用されます。",
         "マウスでワールドマップのカーソルを動かす速さです。すぐに適用されます。"},
    .kbm_modes = {L"自動", L"キーボードとマウス", L"コントローラー"},
    .kbm_actions = {"アクセル", "ブレーキ", "左ステア", "右ステア", "ハンドブレーキ", "シフトアップ", "シフトダウン", "クラッチ", "カメラ切り替え", "巻き戻し", "後方確認", "アクティベート", "フォトモード", "前のステーション", "次のステーション", "マップ", "ポーズ", "メニュー：決定", "メニュー：戻る", "メニュー：上", "メニュー：下", "メニュー：左", "メニュー：右", "メニュー：前のタブ", "メニュー：次のタブ", "メニュー：Xボタン", "メニュー：Yボタン", "メニュー：左トリガー", "メニュー：右トリガー", "メニュー：方向パッド上", "メニュー：方向パッド下", "メニュー：方向パッド左", "メニュー：方向パッド右"},
    .kbm_key_descriptions =
        {"この運転操作のキーです。行で Enter（または左右キー）を押してから新しいキーを押します。Esc で取り消し、Backspace または Delete で解除します。他の運転操作で使用中のキーは使えません。すぐに適用されます。",
         "このメニュー操作のキーです。行で Enter（または左右キー）を押してから新しいキーを押します。Esc で取り消し、Backspace または Delete で解除します。他のメニュー操作で使用中のキーは使えません。すぐに適用されます。"},
    .kbm_reset = "キー割り当てをリセット",
    .kbm_reset_description = "すべてのキー割り当てを初期設定に戻します。Enter または左右キーを押してください。すぐに適用されます。",
    .kbm_labels = {L"キーを押してください...", L"なし", L"使用できないキー", L"キーボードとマウスはオフ", L"保存に失敗しました", L"使用中", L"リセット", L"完了"}};

static const VideoOptionsText korean = {
    .photo_lights = L"전조등",
    .player_character = "플레이어 캐릭터",
    .player_character_description = "플레이어 모델을 선택합니다. 기본값은 게임 설정을 따릅니다. 변경 사항은 플레이어 모델을 다음에 불러올 때 적용됩니다.",
    .open_world_drivatars = "오픈 월드 Drivatar",
    .open_world_drivatars_description = "자유 주행에서 Drivatar를 활성화합니다. 적용하려면 게임을 다시 시작해야 합니다. 레이스 상대나 이벤트 보상에는 영향을 주지 않습니다.",
    .fov_label = "시야각",
    .skill_counter_shake = "스킬 카운터 흔들림",
    .skill_counter_shake_description = "스킬 체인 카운터의 흔들림과 크기 증가를 활성화합니다.",
    .speed_zone_vignette = "스피드 존 비네트",
    .speed_zone_vignette_description = "스피드 존에서 추가 비네트 효과를 활성화합니다.",
    .skill_point_notification = "스킬 포인트 획득 알림",
    .skill_point_description = "스킬 포인트 획득 알림을 표시하고 관련 대사를 재생합니다. 꺼도 스킬 포인트는 계속 지급됩니다.",
    .unit_names = {"출력 단위", "스프링 단위"},
    .unit_descriptions = {"엔진 출력을 표시할 단위를 선택합니다. 기본값은 단위 설정을 따릅니다.", "스프링 강성을 표시할 단위를 선택합니다. 기본값은 단위 설정을 따릅니다."},
    .master_volume = "전체 음량",
    .manufacturer_jump = L"제조사로 이동",
    .event_labels = {L"Drivatar 수", L"출발 순서", L"순정 차량만", L"일반 교통"},
    .grid_choices = {L"무작위", L"낮은 PI 우선", L"높은 PI 우선"},
    .division_label = L"차량 디비전",
    .garage_label = L"차고로 돌아가기",
    .garage_question = L"현재 지도의 페스티벌 차고로 돌아가시겠습니까?",
    .camera = "드리프트 카메라",
    .driver = "운전자",
    .parameters = {"민감도", "시점 이동 속도", "범위"},
    .descriptions = {"드리프트 카메라를 추적 및 운전석 시점에서 켜거나, 운전석 시점에서만 "
                     "켜거나, 끕니다.",
                     "차량 각도와 카메라 각도의 비율입니다. 이 설정을 높이면 차량 각도에 "
                     "대한 "
                     "카메라의 민감도가 "
                     "높아집니다.",
                     "카메라가 원래 위치에서 차량의 이동 방향으로 움직이는 속도입니다. 이 "
                     "설정을 낮추면 방향이 "
                     "바뀔 때 카메라가 더 부드럽게 움직입니다.",
                     "이 설정을 낮추면 차량이 최대 각도에 도달했을 때 카메라의 움직임을 "
                     "제한할 "
                     "수 있습니다."},
    .choices = {L"켜짐", L"운전자", L"꺼짐"},
    .fov_views = {"추적", "원거리 추적", "운전자", "후드", "범퍼"},
    .fov_description = "해당 카메라의 수직 시야각을 도 단위로 설정합니다.",
    .settings_names = {"실시간 그림자", "", "사진 모드 샘플 수", "", "미니맵 방향",
                       "Autodrive 시네마틱 카메라", "챔피언십 대화 건너뛰기", "시작 위치",
                       "차량 변경 시 위치", "4K 해상도 디자인", "향상된 디자인 MSAA",
                       "향상된 디자인 샘플링", "모든 이벤트 표시", "이벤트 중 Autodrive",
                       "사용자 지정 쇼케이스", "사진 모드 벤 대사", "차고 애슐리 대사",
                       "휠스핀 알림", "체크포인트 효과음"},
    .settings_descriptions =
        {"태양 그림자의 부드러운 갱신을 설정합니다. 기본값은 현재 변환 계층의 설정을 사용합니다. 재시작이 필요합니다.",
         "",
         "사진 모드 샘플 수를 설정합니다. 기본값은 현재 변환 계층의 설정을 사용합니다. 재시작이 필요합니다.",
         "",
         "미니맵이 차량 또는 카메라 방향을 따르도록 설정합니다.",
         "Autodrive 중 시네마틱 카메라를 사용합니다.",
         "참가 조건에 맞지 않는 차량으로 챔피언십에 진입할 때의 강제 대화를 건너뜁니다. 재시작이 필요합니다.",
         "게임을 시작할 때의 위치를 선택합니다. 기본값은 평소처럼 게임 종료 시 위치에서 가장 가까운 자동차 모임에서 시작합니다. 변경 사항을 적용하려면 게임을 다시 시작해야 합니다.",
         "오픈 월드에서 차량을 변경할 때 새 차량을 배치할 위치입니다. 두 번째 옵션은 이전 차량이 있던 위치에 그대로 둡니다.",
         "디자인를 4K로 표시합니다. 기본값은 변환 계층 설정을 사용합니다. 재시작이 필요합니다.",
         "디자인에 4x MSAA를 사용합니다. 기본값은 변환 계층 설정을 사용합니다. 재시작이 필요합니다.",
         "디자인에 2x 샘플링을 사용합니다. 기본값은 변환 계층 설정을 사용합니다. 재시작이 필요합니다.",
         "지도에 모든 레이스 설정 지점을 표시합니다. 게임의 모든 레이스를 시작할 수 있습니다.",
         "레이스, 로드 트립 등 경로가 있는 이벤트에서 Autodrive를 허용합니다.",
         "쇼케이스 이벤트에 참가할 차를 내 차고에서 선택할 수 있습니다.",
         "처음 촬영하는 차량이 사진에 없을 때 벤의 대사를 재생합니다.",
         "차고에 들어갈 때 애슐리의 반복 인사 대사를 재생합니다.",
         "자유 주행의 휠스핀 안내를 표시하고 관련 대사를 재생합니다. 꺼도 레이스 후 메시지와 휠스핀 보상은 유지됩니다.",
         "체크포인트를 통과할 때 효과음을 재생합니다."},
    .default_choice = L"기본값",
    .minimap_choices = {L"차량 따라가기", L"카메라 따라가기"},
    .spawn_choices = {L"자동차 모임 입구", L"페스티벌 현장"},
    .car_position_choices = {L"가장 가까운 도로", L"같은 위치"},
    .keyboard_mouse = "키보드 및 마우스",
    .kbm_names = {"입력 모드", "마우스 시점", "마우스 시점 (위/아래)", "마우스 Y축 반전", "마우스 감도", "키보드 조향 속도", "메뉴 포인터", "커서 크기", "지도 커서 속도"},
    .kbm_descriptions =
        {"자동은 마지막으로 사용한 입력으로 전환합니다. 키보드 및 마우스 또는 게임패드는 한 가지만 사용합니다. 즉시 적용됩니다.",
         "주행 중 마우스로 카메라를 돌리며, 잠시 후 시점이 중앙으로 돌아옵니다. 즉시 적용됩니다.",
         "범퍼, 후드, 콕핏, 운전자 시점에서 마우스를 앞뒤로 움직이면 시점이 위아래로 기울어지며, 좌우 시점처럼 원래대로 돌아옵니다. 즉시 적용됩니다.",
         "마우스의 위아래를 바꿉니다: 주행 중 위아래 보기, 사진 모드 카메라, 차량 뷰어 드래그. 지도 커서에는 영향을 주지 않습니다. 즉시 적용됩니다.",
         "마우스 움직임당 시점이 도는 정도입니다. 즉시 적용됩니다.",
         "조향 키가 핸들을 돌리고 되돌리는 속도입니다. 즉시 적용됩니다.",
         "일시 정지 메뉴와 지도의 포인터: 클릭으로 선택, 오른쪽 클릭으로 뒤로, 휠로 스크롤합니다. 즉시 적용됩니다.",
         "메뉴 포인터의 크기입니다. 즉시 적용됩니다.",
         "마우스로 월드 지도 커서를 움직이는 속도입니다. 즉시 적용됩니다."},
    .kbm_modes = {L"자동", L"키보드 및 마우스", L"게임패드"},
    .kbm_actions = {"가속", "브레이크", "왼쪽 조향", "오른쪽 조향", "핸드브레이크", "기어 올림", "기어 내림", "클러치", "카메라 변경", "되감기", "뒤돌아보기", "활성화", "사진 모드", "이전 방송국", "다음 방송국", "지도", "일시 정지", "메뉴: 확인", "메뉴: 뒤로", "메뉴: 위", "메뉴: 아래", "메뉴: 왼쪽", "메뉴: 오른쪽", "메뉴: 이전 탭", "메뉴: 다음 탭", "메뉴: X 버튼", "메뉴: Y 버튼", "메뉴: 왼쪽 트리거", "메뉴: 오른쪽 트리거", "메뉴: 방향 패드 위", "메뉴: 방향 패드 아래", "메뉴: 방향 패드 왼쪽", "메뉴: 방향 패드 오른쪽"},
    .kbm_key_descriptions =
        {"이 주행 동작의 키입니다. 해당 줄에서 Enter(또는 왼쪽/오른쪽)를 누른 뒤 새 키를 누르세요. Esc는 취소, Backspace 또는 Delete는 해제합니다. 다른 주행 동작이 사용하는 키는 거부됩니다. 즉시 적용됩니다.",
         "이 메뉴 동작의 키입니다. 해당 줄에서 Enter(또는 왼쪽/오른쪽)를 누른 뒤 새 키를 누르세요. Esc는 취소, Backspace 또는 Delete는 해제합니다. 다른 메뉴 동작이 사용하는 키는 거부됩니다. 즉시 적용됩니다."},
    .kbm_reset = "키 설정 초기화",
    .kbm_reset_description = "모든 키 설정을 기본값으로 되돌립니다. Enter 또는 왼쪽/오른쪽을 누르세요. 즉시 적용됩니다.",
    .kbm_labels = {L"키를 누르세요...", L"없음", L"사용할 수 없는 키", L"키보드 및 마우스 꺼짐", L"저장 실패", L"사용 중", L"초기화", L"완료"}};

static const VideoOptionsText chinese = {
    .photo_lights = L"頭燈",
    .player_character = "玩家角色",
    .player_character_description = "選擇玩家模型。「預設」遵循遊戲設定。變更會在下次載入玩家模型時套用。",
    .open_world_drivatars = "開放世界 Drivatar",
    .open_world_drivatars_description = "在自由漫遊中啟用 Drivatar。需重新啟動遊戲才會生效。不影響賽事對手或活動獎勵。",
    .fov_label = "視野",
    .skill_counter_shake = "技術計數器晃動",
    .skill_counter_shake_description = "啟用連續技術計數器的晃動與放大效果。",
    .speed_zone_vignette = "測速區間暗角",
    .speed_zone_vignette_description = "啟用測速區間的額外暗角效果。",
    .skill_point_notification = "技術點數獲得通知",
    .skill_point_description = "顯示獲得技術點數的通知並播放相關對白。關閉後仍會獲得技術點數。",
    .unit_names = {"動力單位", "彈簧單位"},
    .unit_descriptions = {"選擇引擎動力的顯示單位。「預設」遵循單位設定。", "選擇彈簧剛性的顯示單位。「預設」遵循單位設定。"},
    .master_volume = "主音量",
    .manufacturer_jump = L"跳至製造商",
    .event_labels = {L"Drivatar 數量", L"起跑順序", L"僅限原廠車", L"車流"},
    .grid_choices = {L"隨機", L"PI 較低優先", L"PI 較高優先"},
    .division_label = L"車輛組別",
    .garage_label = L"返回車庫",
    .garage_question = L"要返回目前地圖上的嘉年華車庫嗎？",
    .camera = "甩尾攝影機",
    .driver = "駕駛",
    .parameters = {"靈敏度", "視角移動速度", "範圍"},
    .descriptions = {"啟用追車與駕駛艙甩尾攝影機、只啟用駕駛艙攝影機，或停用。",
                     "車輛角度與攝影機角度的比例。提高此設定可增加攝影機對車輛"
                     "角度的靈敏度。",
                     "攝影機從原始位置轉向車輛移動方向的速度。降低此設定可讓攝"
                     "影機在方向改變時"
                     "移動得更平順。",
                     "降低此設定可限制車輛達到最大角度時攝影機的移動幅度。"},
    .choices = {L"開啟", L"駕駛", L"關閉"},
    .fov_views = {"追蹤", "遠距離追蹤", "駕駛", "引擎蓋", "保險桿"},
    .fov_description = "以度為單位設定所選攝影機的垂直視野。",
    .settings_names = {"即時陰影", "", "相片模式取樣數", "", "小地圖方向", "Autodrive電影攝影機",
                       "略過錦標賽對話", "開始位置", "換車時的位置", "4K解析度塗裝", "增強塗裝MSAA",
                       "增強塗裝取樣", "顯示所有賽事", "賽事中使用Autodrive", "自訂表演賽",
                       "Ben 的相片模式對話", "Ashley 的車庫對話", "幸運輪盤通知", "檢查點音效"},
    .settings_descriptions =
        {"設定平滑的陽光陰影更新。預設使用目前轉譯層的設定。需要重新啟動遊戲。",
         "",
         "設定相片模式的取樣數。預設使用目前轉譯層的設定。需要重新啟動遊戲。",
         "",
         "選擇小地圖跟隨車輛或攝影機的方向。",
         "Autodrive時使用電影攝影機。",
         "駕駛不符合資格的車輛進入錦標賽時，略過強制對話。需要重新啟動遊戲。",
         "選擇遊戲啟動時的開始位置。預設會像平常一樣，從最接近您關閉遊戲時所在位置的車聚開始。需要重新啟動遊戲才能套用變更。",
         "在開放世界換車時，新車應該出現的位置。後者會讓您留在舊車原本所在的位置。",
         "使用4K塗裝。預設使用目前轉譯層的設定。需要重新啟動遊戲。",
         "塗裝使用4x MSAA。預設使用目前轉譯層的設定。需要重新啟動遊戲。",
         "塗裝使用2x取樣。預設使用目前轉譯層的設定。需要重新啟動遊戲。",
         "在地圖上顯示所有比賽設定地點，可用來開始遊戲中的任何比賽。",
         "允許在比賽、公路旅行及其他設有路線的賽事中使用Autodrive。",
         "可選擇自己車庫中的車輛參加表演賽。",
         "照片中沒有首次拍攝的車輛時，播放班的對白。",
         "進入車庫時，播放艾希莉的例行問候。",
         "顯示自由漫遊的抽獎提示並播放相關對白。關閉後，賽後訊息與抽獎獎勵仍然有效。",
         "通過檢查點時播放音效。"},
    .default_choice = L"預設",
    .minimap_choices = {L"跟隨車輛", L"跟隨攝影機"},
    .spawn_choices = {L"車聚入口", L"嘉年華會場"},
    .car_position_choices = {L"最近的道路", L"相同位置"},
    .keyboard_mouse = "鍵盤與滑鼠",
    .kbm_names = {"輸入模式", "滑鼠視角", "滑鼠視角（上下）", "反轉滑鼠 Y 軸", "滑鼠靈敏度", "鍵盤轉向速度", "選單指標", "游標大小", "地圖游標速度"},
    .kbm_descriptions =
        {"自動會切換到你最後使用的裝置。鍵盤與滑鼠或控制器則固定使用一種。立即套用。",
         "駕駛時以滑鼠轉動鏡頭，片刻後視角會回正。立即套用。",
         "在保險桿、引擎蓋、駕駛艙與車手視角中，前後移動滑鼠會讓視角上下傾斜，並會像左右視角一樣回正。立即套用。",
         "對調滑鼠的上下方向：駕駛時上下觀看、拍照模式鏡頭與拖曳車輛檢視器。不影響地圖游標。立即套用。",
         "每次移動滑鼠時視角轉動的幅度。立即套用。",
         "轉向按鍵轉動方向盤與回正的速度。立即套用。",
         "暫停選單與地圖中的指標：按一下選擇，按右鍵返回，滾輪捲動。立即套用。",
         "選單指標的大小。立即套用。",
         "滑鼠移動世界地圖游標的速度。立即套用。"},
    .kbm_modes = {L"自動", L"鍵盤與滑鼠", L"控制器"},
    .kbm_actions = {"油門", "煞車", "向左轉", "向右轉", "手煞車", "升檔", "降檔", "離合器", "切換鏡頭", "倒轉", "向後看", "啟動", "拍照模式", "上一個電台", "下一個電台", "地圖", "暫停", "選單：確認", "選單：返回", "選單：上", "選單：下", "選單：左", "選單：右", "選單：上一個分頁", "選單：下一個分頁", "選單：X 按鈕", "選單：Y 按鈕", "選單：左扳機", "選單：右扳機", "選單：方向鍵上", "選單：方向鍵下", "選單：方向鍵左", "選單：方向鍵右"},
    .kbm_key_descriptions =
        {"此駕駛動作的按鍵。在該列按 Enter（或左/右），再按新的按鍵；Esc 取消，Backspace 或 Delete 清除。已被其他駕駛動作使用的按鍵會被拒絕。立即套用。",
         "此選單動作的按鍵。在該列按 Enter（或左/右），再按新的按鍵；Esc 取消，Backspace 或 Delete 清除。已被其他選單動作使用的按鍵會被拒絕。立即套用。"},
    .kbm_reset = "重設按鍵設定",
    .kbm_reset_description = "將所有按鍵設定還原為預設值。按 Enter 或左/右。立即套用。",
    .kbm_labels = {L"請按一個鍵...", L"無", L"無法使用此鍵", L"鍵盤與滑鼠已關閉", L"儲存失敗", L"使用中", L"重設", L"完成"}};

static const VideoOptionsText czech = {
    .photo_lights = L"Světlomety",
    .player_character = "Postava hráče",
    .player_character_description = "Vyber model hráče. Výchozí se řídí hrou. Změny se projeví při příštím načtení modelu hráče.",
    .open_world_drivatars = "Drivataři v otevřeném světě",
    .open_world_drivatars_description = "Zapne Drivatary ve volné jízdě. Pro použití změny je nutné restartovat hru. Nemění soupeře v závodech ani odměny za události.",
    .fov_label = "Zorné pole",
    .skill_counter_shake = "Chvění počítadla dovedností",
    .skill_counter_shake_description = "Zapne chvění a zvětšování počítadla řetězce dovedností.",
    .speed_zone_vignette = "Vinětace rychlostních zón",
    .speed_zone_vignette_description = "Zapne dodatečnou vinětaci v rychlostních zónách.",
    .skill_point_notification = "Oznámení o bodu dovednosti",
    .skill_point_description = "Zobrazí oznámení o získaném bodu dovednosti a přehraje příslušnou promluvu. Body se udělují i při vypnuté volbě.",
    .unit_names = {"Jednotky výkonu", "Jednotky pružin"},
    .unit_descriptions = {"Vyber jednotku výkonu motoru. Výchozí se řídí nastavením jednotek.", "Vyber jednotku tuhosti pružin. Výchozí se řídí nastavením jednotek."},
    .master_volume = "Hlavní hlasitost",
    .manufacturer_jump = L"Přejít na výrobce",
    .event_labels = {L"Počet Drivatarů", L"Pořadí na startu", L"Pouze sériová auta", L"Provoz"},
    .grid_choices = {L"Náhodné", L"Nejnižší PI první", L"Nejvyšší PI první"},
    .division_label = L"Divize aut",
    .garage_label = L"Vrátit se do garáže",
    .garage_question = L"Vrátit se do festivalové garáže na aktuální mapě?",
    .camera = "Driftovací kamera",
    .driver = "Řidič",
    .parameters = {"Citlivost", "Rychlost otáčení", "Rozsah"},
    .descriptions = {"Zapněte driftovací kameru při sledování a v kokpitu, pouze v "
                     "kokpitu, nebo ji vypněte.",
                     "Poměr mezi úhlem vozu a úhlem kamery. Zvýšením této hodnoty zvýšíte "
                     "citlivost kamery na úhel "
                     "vozu.",
                     "Rychlost, jakou se kamera otáčí z původní polohy do směru pohybu. "
                     "Snížením této hodnoty "
                     "vyhladíte pohyb kamery při změnách směru.",
                     "Snížením této hodnoty omezíte pohyb kamery, když vůz dosáhne "
                     "maximálního "
                     "úhlu."},
    .choices = {L"Zapnuto", L"Řidič", L"Vypnuto"},
    .fov_views = {"Sledování", "Vzdálené sledování", "Řidič", "Kapota", "Nárazník"},
    .fov_description = "Nastavte svislé zorné pole vybrané kamery ve stupních.",
    .settings_names = {"Stíny v reálném čase", "", "Vzorky fotografického režimu", "",
                       "Orientace minimapy", "Filmová kamera Autodrive",
                       "Přeskočit dialog šampionátu", "Počáteční poloha", "Poloha při změně auta",
                       "Polepy v rozlišení 4K", "Vylepšené MSAA polepů",
                       "Vylepšené vzorkování polepů", "Zobrazit všechny závody",
                       "Autodrive v závodech", "Vlastní exhibice", "Benovy dialogy ve fotorežimu",
                       "Ashleyiny dialogy v garáži", "Oznámení o roztočení kola",
                       "Zvuk kontrolního bodu"},
    .settings_descriptions =
        {"Nastaví plynulé sluneční stíny. Výchozí použije nastavení aktuální překladové vrstvy. Vyžaduje restart.",
         "",
         "Nastaví počet vzorků fotorežimu. Výchozí použije nastavení aktuální překladové vrstvy. Vyžaduje restart.",
         "",
         "Zvolte, zda minimapa sleduje auto, nebo kameru.",
         "Používá filmové kamery během Autodrive.",
         "Přeskočí vynucený dialog při vstupu do závodu šampionátu s nezpůsobilým autem. Vyžaduje restart.",
         "Vyberte polohu při spuštění hry. Výchozí vás jako obvykle umístí na sraz aut nejbližší vaší poloze při ukončení hry. Změny se projeví po restartu hry.",
         "Kde má stát nové auto při změně auta v otevřeném světě. Druhá možnost vás ponechá přesně na místě předchozího auta.",
         "Použije 4K polepy. Výchozí přebírá nastavení překladové vrstvy. Vyžaduje restart.",
         "Použije 4x MSAA polepů. Výchozí přebírá nastavení překladové vrstvy. Vyžaduje restart.",
         "Použije 2x vzorkování polepů. Výchozí přebírá nastavení překladové vrstvy. Vyžaduje restart.",
         "Zobrazí na mapě všechny body pro nastavení závodů. Umožňuje spustit libovolný závod ve hře.",
         "Povolit Autodrive v závodech, výletech a dalších událostech s trasou.",
         "Umožní vybrat vlastní vůz pro exhibiční závody.",
         "Přehraje Benovu promluvu při pořízení fotografie bez poprvé vyfotografovaných aut.",
         "Přehraje Ashleyina opakovaná uvítání při vstupu do garáže.",
         "Zobrazí výzvu k roztočení kola ve volné jízdě a přehraje příslušnou promluvu. Zprávy po závodech a odměny z kola zůstávají aktivní i při vypnuté volbě.",
         "Přehraje zvukový efekt při projetí kontrolního bodu."},
    .default_choice = L"Výchozí",
    .minimap_choices = {L"Sledovat auto", L"Sledovat kameru"},
    .spawn_choices = {L"Vjezd na sraz aut", L"Areál festivalu"},
    .car_position_choices = {L"Nejbližší silnice", L"Stejná poloha"},
    .keyboard_mouse = "KLÁVESNICE A MYŠ",
    .kbm_names = {"Režim ovládání", "Rozhlížení myší", "Rozhlížení myší nahoru/dolů", "Obrátit osu Y myši", "Citlivost myši", "Rychlost řízení klávesnicí", "Ukazatel v menu", "Velikost kurzoru", "Rychlost kurzoru na mapě"},
    .kbm_descriptions =
        {"Auto přepne na to, co jste použili naposledy. Klávesnice a myš nebo Ovladač drží jeden zdroj. Platí okamžitě.",
         "Při jízdě myš otáčí kamerou a pohled se po chvíli vrátí na střed. Platí okamžitě.",
         "V pohledu z nárazníku, kapoty, kokpitu a řidiče pohyb myši dopředu a dozadu naklání pohled nahoru a dolů; vrací se jako rozhlížení do stran. Platí okamžitě.",
         "Prohodí nahoru a dolů pro myš: rozhlížení nahoru a dolů při jízdě, kameru fotorežimu a otáčení v prohlížeči aut. Kurzor mapy se nemění. Platí okamžitě.",
         "Jak moc se pohled otočí při pohybu myši. Platí okamžitě.",
         "Jak rychle klávesy řízení točí volantem a nechávají ho vrátit. Platí okamžitě.",
         "Ukazatel v menu pauzy a na mapě: kliknutí vybírá, pravé tlačítko vrací zpět, kolečko posouvá. Platí okamžitě.",
         "Velikost ukazatele v menu. Platí okamžitě.",
         "Jak rychle myš posouvá kurzor na mapě světa. Platí okamžitě."},
    .kbm_modes = {L"Auto", L"Klávesnice a myš", L"Ovladač"},
    .kbm_actions = {"Plyn", "Brzda", "Zatočit vlevo", "Zatočit vpravo", "Ruční brzda", "Řadit nahoru", "Řadit dolů", "Spojka", "Změnit kameru", "Přetočit", "Pohled dozadu", "Aktivovat", "Fotorežim", "Předchozí stanice", "Další stanice", "Mapa", "Pauza", "Menu: potvrdit", "Menu: zpět", "Menu: nahoru", "Menu: dolů", "Menu: vlevo", "Menu: vpravo", "Menu: předchozí karta", "Menu: další karta", "Menu: tlačítko X", "Menu: tlačítko Y", "Menu: levá spoušť", "Menu: pravá spoušť", "Menu: směrový ovladač nahoru", "Menu: směrový ovladač dolů", "Menu: směrový ovladač vlevo", "Menu: směrový ovladač vpravo"},
    .kbm_key_descriptions =
        {"Klávesa pro tuto akci jízdy. Na řádku stiskněte Enter (nebo vlevo/vpravo) a pak novou klávesu; Esc zruší, Backspace nebo Delete ji vymaže. Klávesa použitá jinou akcí jízdy je odmítnuta. Platí okamžitě.",
         "Klávesa pro tuto akci menu. Na řádku stiskněte Enter (nebo vlevo/vpravo) a pak novou klávesu; Esc zruší, Backspace nebo Delete ji vymaže. Klávesa použitá jinou akcí menu je odmítnuta. Platí okamžitě."},
    .kbm_reset = "Obnovit klávesy",
    .kbm_reset_description = "Vrátí všechna přiřazení kláves na výchozí. Stiskněte Enter nebo vlevo/vpravo. Platí okamžitě.",
    .kbm_labels = {L"Stiskněte klávesu...", L"Žádná", L"Klávesa není dostupná", L"Klávesnice a myš vyp.", L"Uložení selhalo", L"Používá", L"Obnovit", L"Hotovo"}};

static const VideoOptionsText hungarian = {
    .photo_lights = L"Fényszórók",
    .player_character = "Játékoskarakter",
    .player_character_description = "Válaszd ki a játékos modelljét. Az alapértelmezett a játék beállítását követi. A változtatás a játékosmodell következő betöltésekor lép életbe.",
    .open_world_drivatars = "Drivatarok a nyílt világban",
    .open_world_drivatars_description = "Bekapcsolja a Drivatarokat szabad autózás közben. A módosításhoz újra kell indítani a játékot. Nem változtatja meg a versenyellenfeleket vagy az események jutalmait.",
    .fov_label = "Látómező",
    .skill_counter_shake = "Ügyességszámláló rázkódása",
    .skill_counter_shake_description = "Bekapcsolja az ügyességlánc számlálójának rázkódását és méretnövekedését.",
    .speed_zone_vignette = "Sebességzóna-vignetta",
    .speed_zone_vignette_description = "Bekapcsolja a sebességzónák további vignettaeffektusát.",
    .skill_point_notification = "Ügyességpont-értesítés",
    .skill_point_description = "Megjeleníti a megszerzett ügyességpont értesítését, és lejátssza a hozzá tartozó párbeszédet. A pontok kikapcsolt állapotban is járnak.",
    .unit_names = {"Teljesítmény mértékegysége", "Rugók mértékegysége"},
    .unit_descriptions = {"Válaszd ki a motorteljesítmény mértékegységét. Az alapértelmezett a mértékegység-beállítást követi.", "Válaszd ki a rugómerevség mértékegységét. Az alapértelmezett a mértékegység-beállítást követi."},
    .master_volume = "Főhangerő",
    .manufacturer_jump = L"Ugrás a gyártóhoz",
    .event_labels = {L"Drivatarok száma", L"Rajtsorrend", L"Csak gyári autók", L"Forgalom"},
    .grid_choices = {L"Véletlenszerű", L"Alacsonyabb PI elöl", L"Magasabb PI elöl"},
    .division_label = L"Autóosztály",
    .garage_label = L"Vissza a garázsba",
    .garage_question = L"Visszatérsz az aktuális térkép fesztiválgarázsába?",
    .camera = "Driftkamera",
    .driver = "Vezető",
    .parameters = {"Érzékenység", "Nézetváltás sebessége", "Tartomány"},
    .descriptions = {"Kapcsold be a driftkamerát a követő és a pilótafülke "
                     "nézetben, csak a pilótafülkében, vagy kapcsold ki.",
                     "Az autó szöge és a kamera szöge közötti arány. Növeld "
                     "ezt az értéket, "
                     "hogy a kamera "
                     "érzékenyebben reagáljon az autó szögére.",
                     "A sebesség, amellyel a kamera az eredeti helyzetéből a "
                     "haladás iránya "
                     "felé fordul. Az érték "
                     "csökkentésével simábbá teheted a kamera mozgását az "
                     "irányváltások során.",
                     "Csökkentsd ezt az értéket, hogy korlátozd a kamera "
                     "mozgását, amikor az "
                     "autó eléri a "
                     "legnagyobb szöget."},
    .choices = {L"Be", L"Vezető", L"Ki"},
    .fov_views = {"Követő", "Távoli követő", "Vezető", "Motorháztető", "Lökhárító"},
    .fov_description = "Állítsd be a kiválasztott kamera függőleges látószögét fokban.",
    .settings_names = {"Valós idejű árnyékok", "", "Fotómód mintái", "", "Minitérkép tájolása",
                       "Autodrive filmes kamerája", "Bajnokság párbeszédének kihagyása",
                       "Kezdőhely", "Autócsere helye", "4K felbontású festések",
                       "Javított festés-MSAA", "Javított festés-mintavételezés",
                       "Minden esemény megjelenítése", "Autodrive eseményeken",
                       "Egyéni bemutatóversenyek", "Ben fotómód-párbeszédei",
                       "Ashley garázspárbeszédei", "Nyereménykerék-értesítés",
                       "Ellenőrzőpont hangeffektje"},
    .settings_descriptions =
        {"A napárnyékok simítása. Az alapérték az aktuális fordítási réteg beállítását használja. Újraindítás szükséges.",
         "",
         "A fotómód mintaszáma. Az alapérték az aktuális fordítási réteg beállítását használja. Újraindítás szükséges.",
         "",
         "Válaszd ki, hogy a minitérkép az autót vagy a kamerát kövesse.",
         "Filmes kamerákat használ az Autodrive során.",
         "Kihagyja a kötelező párbeszédet, ha nem megfelelő autóval lépsz be egy bajnoki versenybe. Újraindítás szükséges.",
         "Válaszd ki a játék indításakor használt kezdőhelyet. Az alapérték a megszokott módon a kilépéskori helyedhez legközelebbi autóstalálkozón indít. A módosításokhoz újra kell indítani a játékot.",
         "Az új autó helye, amikor a nyílt világban autót váltasz. A második lehetőség pontosan ott hagy, ahol az előző autó volt.",
         "4K festéseket használ. Az alapérték a fordítási réteg beállítását követi. Újraindítás szükséges.",
         "4x MSAA-t használ. Az alapérték a fordítási réteg beállítását követi. Újraindítás szükséges.",
         "2x mintavételezést használ. Az alapérték a fordítási réteg beállítását követi. Újraindítás szükséges.",
         "Megjeleníti az összes versenybeállítási pontot a térképen, így bármelyik verseny elindítható.",
         "Autodrive engedélyezése versenyeken, túrákon és más útvonallal rendelkező eseményeken.",
         "Saját autót választhatsz a bemutatóversenyekhez.",
         "Lejátssza Ben párbeszédét, ha a fotón nincs először lefényképezett autó.",
         "Lejátssza Ashley visszatérő üdvözléseit a garázsba lépéskor.",
         "Megjeleníti a kerékpörgetés üzenetét szabad autózás közben, és lejátssza a hozzá tartozó párbeszédet. A verseny utáni üzenetek és a pörgetés jutalmai kikapcsolva is megmaradnak.",
         "Lejátssza a hangeffektust az ellenőrzőpontokon való áthaladáskor."},
    .default_choice = L"Alapérték",
    .minimap_choices = {L"Autó követése", L"Kamera követése"},
    .spawn_choices = {L"Autóstalálkozó bejárata", L"Fesztivál helyszíne"},
    .car_position_choices = {L"Legközelebbi út", L"Azonos hely"},
    .keyboard_mouse = "BILLENTYŰZET ÉS EGÉR",
    .kbm_names = {"Bemeneti mód", "Egeres nézelődés", "Egeres nézelődés fel/le", "Egér Y tengelyének megfordítása", "Egér érzékenysége", "Billentyűzetes kormányzás sebessége", "Menümutató", "Kurzor mérete", "Térképkurzor sebessége"},
    .kbm_descriptions =
        {"Az Auto arra vált, amit utoljára használtál. A Billentyűzet és egér vagy a Kontroller egy forrást tart meg. Azonnal érvényes.",
         "Vezetés közben az egér forgatja a kamerát, a nézet rövid idő után visszaáll középre. Azonnal érvényes.",
         "A lökhárító-, motorháztető-, pilótafülke- és vezetőnézetben az egér előre-hátra mozgatása fel-le dönti a nézetet; az oldalirányú nézelődéshez hasonlóan visszaáll. Azonnal érvényes.",
         "Felcseréli az egér fel és le irányát: fel-le nézés vezetés közben, a fotómód kamerája és az autónézegető forgatása. A térképkurzort nem érinti. Azonnal érvényes.",
         "Mennyit fordul a nézet egy egérmozdulatra. Azonnal érvényes.",
         "Milyen gyorsan fordítják el a kormányzás billentyűi a kormányt, és engedik vissza. Azonnal érvényes.",
         "Mutató a szünetmenüben és a térképen: kattintás a választáshoz, jobb kattintás a visszalépéshez, görgő a görgetéshez. Azonnal érvényes.",
         "A menümutató mérete. Azonnal érvényes.",
         "Milyen gyorsan mozgatja az egér a világtérkép kurzorát. Azonnal érvényes."},
    .kbm_modes = {L"Auto", L"Billentyűzet és egér", L"Kontroller"},
    .kbm_actions = {"Gáz", "Fék", "Kormányzás balra", "Kormányzás jobbra", "Kézifék", "Felváltás", "Visszaváltás", "Kuplung", "Kameraváltás", "Visszatekerés", "Hátranézés", "Aktiválás", "Fotómód", "Előző állomás", "Következő állomás", "Térkép", "Szünet", "Menü: jóváhagyás", "Menü: vissza", "Menü: fel", "Menü: le", "Menü: balra", "Menü: jobbra", "Menü: előző lap", "Menü: következő lap", "Menü: X gomb", "Menü: Y gomb", "Menü: bal ravasz", "Menü: jobb ravasz", "Menü: iránygomb fel", "Menü: iránygomb le", "Menü: iránygomb balra", "Menü: iránygomb jobbra"},
    .kbm_key_descriptions =
        {"Ennek a vezetési műveletnek a billentyűje. A sorban nyomd meg az Entert (vagy a balra/jobbra gombot), majd az új billentyűt; az Esc megszakítja, a Backspace vagy a Delete törli. Más vezetési művelet által használt billentyűt nem fogad el. Azonnal érvényes.",
         "Ennek a menüműveletnek a billentyűje. A sorban nyomd meg az Entert (vagy a balra/jobbra gombot), majd az új billentyűt; az Esc megszakítja, a Backspace vagy a Delete törli. Más menüművelet által használt billentyűt nem fogad el. Azonnal érvényes."},
    .kbm_reset = "Billentyűk visszaállítása",
    .kbm_reset_description = "Minden billentyű-hozzárendelést visszaállít az alapértékre. Nyomd meg az Entert vagy a balra/jobbra gombot. Azonnal érvényes.",
    .kbm_labels = {L"Nyomj meg egy billentyűt...", L"Nincs", L"Nem elérhető billentyű", L"Billentyűzet és egér ki", L"Mentés sikertelen", L"Foglalt", L"Visszaállítás", L"Kész"}};

static const VideoOptionsText polish = {
    .photo_lights = L"Reflektory",
    .player_character = "Postać gracza",
    .player_character_description = "Wybierz model gracza. Ustawienie domyślne jest zgodne z grą. Zmiany zostaną zastosowane przy następnym wczytaniu modelu gracza.",
    .open_world_drivatars = "Drivatary w otwartym świecie",
    .open_world_drivatars_description = "Włącza Drivatary podczas swobodnej jazdy. Wymaga ponownego uruchomienia gry. Nie zmienia rywali w wyścigach ani nagród za wydarzenia.",
    .fov_label = "Pole widzenia",
    .skill_counter_shake = "Drgania licznika umiejętności",
    .skill_counter_shake_description = "Włącza drgania i powiększanie licznika serii umiejętności.",
    .speed_zone_vignette = "Winieta stref prędkości",
    .speed_zone_vignette_description = "Włącza dodatkową winietę w strefach prędkości.",
    .skill_point_notification = "Powiadomienie o punkcie umiejętności",
    .skill_point_description = "Wyświetla powiadomienie o zdobytym punkcie umiejętności i odtwarza towarzyszącą wypowiedź. Punkty są przyznawane także po wyłączeniu tej opcji.",
    .unit_names = {"Jednostki mocy", "Jednostki sprężyn"},
    .unit_descriptions = {"Wybierz jednostkę mocy silnika. Ustawienie domyślne jest zgodne z ustawieniem jednostek.", "Wybierz jednostkę sztywności sprężyn. Ustawienie domyślne jest zgodne z ustawieniem jednostek."},
    .master_volume = "Głośność ogólna",
    .manufacturer_jump = L"Przejdź do producenta",
    .event_labels = {L"Liczba Drivatarów", L"Kolejność na starcie", L"Tylko auta fabryczne",
                     L"Ruch uliczny"},
    .grid_choices = {L"Losowa", L"Najniższy PI pierwszy", L"Najwyższy PI pierwszy"},
    .division_label = L"Dywizja samochodu",
    .garage_label = L"Wróć do garażu",
    .garage_question = L"Wrócić do garażu festiwalowego na bieżącej mapie?",
    .camera = "Kamera driftu",
    .driver = "Kierowca",
    .parameters = {"Czułość", "Szybkość obrotu", "Zakres"},
    .descriptions = {"Włącz kamerę driftu w widoku pościgowym i z kokpitu, tylko z kokpitu "
                     "lub wyłącz ją.",
                     "Stosunek kąta samochodu do kąta kamery. Zwiększ tę wartość, aby "
                     "kamera "
                     "była bardziej czuła "
                     "na kąt samochodu.",
                     "Szybkość, z jaką kamera obraca się z pierwotnego położenia w "
                     "kierunku "
                     "ruchu. Zmniejszenie "
                     "tej wartości wygładzi ruch kamery przy zmianach kierunku.",
                     "Zmniejsz tę wartość, aby ograniczyć ruch kamery, gdy samochód "
                     "osiągnie "
                     "maksymalny kąt."},
    .choices = {L"Wł.", L"Kierowca", L"Wył."},
    .fov_views = {"Pościg", "Daleki pościg", "Kierowca", "Maska", "Zderzak"},
    .fov_description = "Ustaw pionowe pole widzenia wybranej kamery w stopniach.",
    .settings_names = {"Cienie w czasie rzeczywistym", "", "Próbki trybu fotograficznego", "",
                       "Orientacja minimapy", "Filmowa kamera Autodrive", "Pomiń dialog mistrzostw",
                       "Miejsce startu", "Pozycja po zmianie samochodu",
                       "Malowania w rozdzielczości 4K", "Ulepszone MSAA malowań",
                       "Ulepszone próbkowanie malowań", "Pokaż wszystkie zawody",
                       "Autodrive w zawodach", "Własne wyścigi pokazowe",
                       "Dialogi Bena w trybie zdjęć", "Dialogi Ashley w garażu",
                       "Powiadomienie o losowaniu", "Dźwięk punktu kontrolnego"},
    .settings_descriptions =
        {"Płynne cienie słońca. Domyślne używa ustawienia bieżącej warstwy translacji. Wymagany restart gry.",
         "",
         "Próbki trybu zdjęć. Domyślne używa ustawienia bieżącej warstwy translacji. Wymagany restart gry.",
         "",
         "Wybierz, czy minimapa ma śledzić samochód, czy kamerę.",
         "Używa filmowych kamer podczas Autodrive.",
         "Pomija obowiązkowy dialog przy wjeździe do wyścigu mistrzostw nieodpowiednim samochodem. Wymaga restartu.",
         "Wybierz miejsce startu po uruchomieniu gry. Domyślne umieszcza cię jak zwykle na zlocie najbliższym pozycji przy zamknięciu gry. Uruchom grę ponownie, aby zastosować zmiany.",
         "Miejsce nowego samochodu po zmianie auta w otwartym świecie. Druga opcja pozostawia cię dokładnie tam, gdzie był poprzedni samochód.",
         "Używa malowań 4K. Domyślne stosuje ustawienia warstwy translacji. Wymagany restart.",
         "Używa MSAA 4x. Domyślne stosuje ustawienia warstwy translacji. Wymagany restart.",
         "Używa próbkowania 2x. Domyślne stosuje ustawienia warstwy translacji. Wymagany restart.",
         "Pokazuje na mapie wszystkie punkty konfiguracji wyścigów. Umożliwia rozpoczęcie dowolnego wyścigu w grze.",
         "Pozwól na Autodrive w wyścigach, podróżach i innych zawodach z trasą.",
         "Pozwala wybrać własny samochód do wyścigów pokazowych.",
         "Odtwarza wypowiedź Bena, gdy na zdjęciu nie ma żadnego auta sfotografowanego po raz pierwszy.",
         "Odtwarza powtarzające się powitania Ashley przy wejściu do garażu.",
         "Wyświetla zachętę do losowania podczas swobodnej jazdy i odtwarza towarzyszącą wypowiedź. Wiadomości po wyścigach i nagrody z losowań pozostają aktywne po wyłączeniu tej opcji.",
         "Odtwarza efekt dźwiękowy podczas przekraczania punktu kontrolnego."},
    .default_choice = L"Domyślne",
    .minimap_choices = {L"Śledź samochód", L"Śledź kamerę"},
    .spawn_choices = {L"Wjazd na zlot", L"Teren festiwalu"},
    .car_position_choices = {L"Najbliższa droga", L"To samo miejsce"},
    .keyboard_mouse = "KLAWIATURA I MYSZ",
    .kbm_names = {"Źródło sterowania", "Rozglądanie myszą", "Rozglądanie góra/dół", "Odwróć oś Y myszy", "Czułość myszy", "Szybkość skrętu z klawiatury", "Wskaźnik w menu", "Rozmiar kursora", "Szybkość kursora na mapie"},
    .kbm_descriptions =
        {"Auto przełącza na to, czego dotkniesz ostatnio. Klawiatura i mysz albo Pad trzymają jedno źródło. Działa od razu.",
         "Podczas jazdy mysz obraca kamerę, a widok wraca po chwili. Działa od razu.",
         "W widoku ze zderzaka, maski, kokpitu i kierowcy ruch myszy w przód i w tył pochyla widok w górę i w dół; wraca jak rozglądanie na boki. Działa od razu.",
         "Zamienia górę i dół dla myszy: rozglądanie podczas jazdy, kamera trybu zdjęć i obracanie podglądu auta. Nie dotyczy kursora mapy. Działa od razu.",
         "Jak daleko obraca się widok przy ruchu myszy. Działa od razu.",
         "Jak szybko klawisze skrętu skręcają koła i jak szybko wracają. Działa od razu.",
         "Wskaźnik w menu pauzy i na mapie: klik wybiera, prawy przycisk wraca, kółko przewija. Działa od razu.",
         "Rozmiar wskaźnika w menu. Działa od razu.",
         "Jak szybko mysz przesuwa kursor na mapie świata. Działa od razu."},
    .kbm_modes = {L"Auto", L"Klawiatura i mysz", L"Pad"},
    .kbm_actions = {"Gaz", "Hamulec", "Skręt w lewo", "Skręt w prawo", "Hamulec ręczny", "Bieg w górę", "Bieg w dół", "Sprzęgło", "Zmiana kamery", "Przewijanie czasu", "Patrz do tyłu", "Aktywuj", "Tryb zdjęć", "Poprzednia stacja", "Następna stacja", "Mapa", "Pauza", "Menu: zatwierdź", "Menu: wstecz", "Menu: w górę", "Menu: w dół", "Menu: w lewo", "Menu: w prawo", "Menu: poprzednia karta", "Menu: następna karta", "Menu: przycisk X", "Menu: przycisk Y", "Menu: lewy spust", "Menu: prawy spust", "Menu: krzyżak w górę", "Menu: krzyżak w dół", "Menu: krzyżak w lewo", "Menu: krzyżak w prawo"},
    .kbm_key_descriptions =
        {"Klawisz tej akcji jazdy. Naciśnij Enter (lub strzałkę w lewo/prawo) na wierszu, potem nowy klawisz; Esc anuluje, Backspace lub Delete czyści przypisanie. Klawisz zajęty przez inną akcję jazdy jest odrzucany. Działa od razu.",
         "Klawisz tej akcji menu. Naciśnij Enter (lub strzałkę w lewo/prawo) na wierszu, potem nowy klawisz; Esc anuluje, Backspace lub Delete czyści przypisanie. Klawisz zajęty przez inną akcję menu jest odrzucany. Działa od razu."},
    .kbm_reset = "Przywróć domyślne klawisze",
    .kbm_reset_description = "Przywraca domyślne przypisania wszystkich klawiszy. Naciśnij Enter lub strzałkę w lewo/prawo. Działa od razu.",
    .kbm_labels = {L"Naciśnij klawisz...", L"Brak", L"Klawisz niedostępny", L"Klawiatura i mysz wył.", L"Błąd zapisu", L"Zajęty", L"Przywróć", L"Przywrócono"}};

static const VideoOptionsText russian = {
    .photo_lights = L"Фары",
    .player_character = "Персонаж игрока",
    .player_character_description = "Выберите модель игрока. По умолчанию используется выбор игры. Изменения вступят в силу при следующей загрузке модели игрока.",
    .open_world_drivatars = "Дриватары в открытом мире",
    .open_world_drivatars_description = "Включает дриватаров в свободной поездке. Для применения требуется перезапуск игры. Не влияет на соперников в гонках и награды за события.",
    .fov_label = "Поле зрения",
    .skill_counter_shake = "Тряска счётчика навыков",
    .skill_counter_shake_description = "Включает тряску и увеличение счётчика цепочки навыков.",
    .speed_zone_vignette = "Виньетка скоростных зон",
    .speed_zone_vignette_description = "Включает дополнительную виньетку в скоростных зонах.",
    .skill_point_notification = "Уведомление об очке навыка",
    .skill_point_description = "Показывает уведомление о полученном очке навыка и воспроизводит сопровождающую реплику. Очки начисляются и при отключённой настройке.",
    .unit_names = {"Единицы мощности", "Единицы жёсткости пружин"},
    .unit_descriptions = {"Выберите единицы мощности двигателя. По умолчанию используются общие настройки единиц измерения.", "Выберите единицы жёсткости пружин. По умолчанию используются общие настройки единиц измерения."},
    .master_volume = "Общая громкость",
    .manufacturer_jump = L"Перейти к производителю",
    .event_labels = {L"Количество Drivatar", L"Порядок на старте", L"Только серийные машины",
                     L"Трафик"},
    .grid_choices = {L"Случайный", L"Сначала низкий PI", L"Сначала высокий PI"},
    .division_label = L"Дивизион автомобилей",
    .garage_label = L"Вернуться в гараж",
    .garage_question = L"Вернуться в гараж фестиваля на текущей карте?",
    .camera = "Дрифт-камера",
    .driver = "Водитель",
    .parameters = {"Чувствительность", "Скорость поворота", "Диапазон"},
    .descriptions = {"Включите дрифт-камеру для вида сзади и из кабины, только из кабины "
                     "или отключите её.",
                     "Соотношение угла автомобиля и угла камеры. Увеличьте это значение, "
                     "чтобы "
                     "камера стала более "
                     "чувствительной к углу автомобиля.",
                     "Скорость, с которой камера поворачивается из исходного положения в "
                     "направлении движения. "
                     "Уменьшение этого значения делает движение камеры при смене "
                     "направления "
                     "более плавным.",
                     "Уменьшите это значение, чтобы ограничить движение камеры, когда "
                     "автомобиль достигает "
                     "максимального угла."},
    .choices = {L"Вкл.", L"Водитель", L"Выкл."},
    .fov_views = {"Вид сзади", "Дальний вид сзади", "Водитель", "Капот", "Бампер"},
    .fov_description = "Установите вертикальный угол обзора выбранной камеры в градусах.",
    .settings_names = {"Тени в реальном времени", "", "Выборки фоторежима", "",
                       "Ориентация мини-карты", "Кинематографическая камера Autodrive",
                       "Пропуск диалога чемпионата", "Место появления",
                       "Положение при смене машины", "Ливреи в разрешении 4K",
                       "Улучшенное MSAA ливрей", "Улучшенная выборка ливрей",
                       "Показать все события", "Autodrive в событиях",
                       "Своя машина в показательных заездах", "Диалоги Бена в фоторежиме",
                       "Диалоги Эшли в гараже", "Уведомление о рулетке", "Звук контрольной точки"},
    .settings_descriptions =
        {"Плавное обновление теней. По умолчанию используется настройка текущего слоя трансляции. Нужен перезапуск.",
         "",
         "Число выборок фоторежима. По умолчанию используется настройка текущего слоя трансляции. Нужен перезапуск.",
         "",
         "Выберите, следует ли мини-карта за автомобилем или камерой.",
         "Использует кинематографические камеры во время Autodrive.",
         "Пропускает обязательный диалог при въезде в гонку чемпионата на неподходящем автомобиле. Требуется перезапуск.",
         "Выберите место появления при запуске игры. По умолчанию вы, как обычно, появляетесь на автосходке, ближайшей к месту выхода из игры. Для применения изменений перезапустите игру.",
         "Где должна находиться новая машина при смене машины в открытом мире. Второй вариант оставляет вас точно там, где была предыдущая машина.",
         "Ливреи в 4K. По умолчанию: настройки слоя трансляции. Требуется перезапуск.",
         "MSAA 4x для ливрей. По умолчанию: настройки слоя трансляции. Требуется перезапуск.",
         "Выборка 2x для ливрей. По умолчанию: настройки слоя трансляции. Требуется перезапуск.",
         "Показывает на карте все точки настройки гонок. Позволяет запустить любую гонку в игре.",
         "Разрешить Autodrive в гонках, поездках и других событиях с маршрутом.",
         "Позволяет выбирать свою машину для показательных заездов.",
         "Воспроизводит реплику Бена, если на снимке нет машин, сфотографированных впервые.",
         "Воспроизводит повторяющиеся приветствия Эшли при входе в гараж.",
         "Показывает приглашение к розыгрышу в свободной поездке и воспроизводит сопровождающую реплику. Сообщения после гонок и награды за розыгрыши сохраняются при отключённой настройке.",
         "Воспроизводит звуковой эффект при пересечении контрольной точки."},
    .default_choice = L"По умолчанию",
    .minimap_choices = {L"Следовать за автомобилем", L"Следовать за камерой"},
    .spawn_choices = {L"Въезд на автосходку", L"Площадка фестиваля"},
    .car_position_choices = {L"Ближайшая дорога", L"То же место"},
    .keyboard_mouse = "КЛАВИАТУРА И МЫШЬ",
    .kbm_names = {"Режим ввода", "Обзор мышью", "Обзор мышью вверх/вниз", "Инвертировать ось Y мыши", "Чувствительность мыши", "Скорость руления с клавиатуры", "Указатель в меню", "Размер курсора", "Скорость курсора на карте"},
    .kbm_descriptions =
        {"Авто переключается на то, чем вы пользовались последним. Клавиатура и мышь или Геймпад оставляют один источник. Применяется сразу.",
         "Во время езды мышь поворачивает камеру, а вид через мгновение возвращается к центру. Применяется сразу.",
         "В видах с бампера, капота, из кокпита и от водителя движение мыши вперёд и назад наклоняет вид вверх и вниз; он возвращается так же, как обзор по сторонам. Применяется сразу.",
         "Меняет местами верх и низ для мыши: обзор вверх и вниз во время езды, камера фоторежима и вращение в просмотре автомобиля. Курсор карты не затрагивается. Применяется сразу.",
         "Насколько поворачивается вид при движении мыши. Применяется сразу.",
         "Как быстро клавиши руления поворачивают руль и дают ему вернуться. Применяется сразу.",
         "Указатель в меню паузы и на карте: щелчок — выбор, правый щелчок — назад, колесо — прокрутка. Применяется сразу.",
         "Размер указателя в меню. Применяется сразу.",
         "Как быстро мышь двигает курсор на карте мира. Применяется сразу."},
    .kbm_modes = {L"Авто", L"Клавиатура и мышь", L"Геймпад"},
    .kbm_actions = {"Газ", "Тормоз", "Руль влево", "Руль вправо", "Ручной тормоз", "Передача вверх", "Передача вниз", "Сцепление", "Сменить камеру", "Перемотка", "Взгляд назад", "Активировать", "Фоторежим", "Предыдущая станция", "Следующая станция", "Карта", "Пауза", "Меню: подтвердить", "Меню: назад", "Меню: вверх", "Меню: вниз", "Меню: влево", "Меню: вправо", "Меню: предыдущая вкладка", "Меню: следующая вкладка", "Меню: кнопка X", "Меню: кнопка Y", "Меню: левый триггер", "Меню: правый триггер", "Меню: крестовина вверх", "Меню: крестовина вниз", "Меню: крестовина влево", "Меню: крестовина вправо"},
    .kbm_key_descriptions =
        {"Клавиша для этого действия вождения. Нажмите Enter (или влево/вправо) на строке, затем новую клавишу; Esc отменяет, Backspace или Delete очищает. Клавиша, занятая другим действием вождения, не принимается. Применяется сразу.",
         "Клавиша для этого действия меню. Нажмите Enter (или влево/вправо) на строке, затем новую клавишу; Esc отменяет, Backspace или Delete очищает. Клавиша, занятая другим действием меню, не принимается. Применяется сразу."},
    .kbm_reset = "Сбросить назначения клавиш",
    .kbm_reset_description = "Возвращает все назначения клавиш к стандартным. Нажмите Enter или влево/вправо. Применяется сразу.",
    .kbm_labels = {L"Нажмите клавишу...", L"Нет", L"Клавиша недоступна", L"Клавиатура и мышь выкл.", L"Ошибка сохранения", L"Занята", L"Сбросить", L"Сброшено"}};

static const VideoOptionsText dutch = {
    .photo_lights = L"Koplampen",
    .player_character = "Spelerspersonage",
    .player_character_description = "Kies het spelersmodel. Standaard volgt het spel. Wijzigingen gelden zodra het spelersmodel opnieuw wordt geladen.",
    .open_world_drivatars = "Drivatars in de open wereld",
    .open_world_drivatars_description = "Schakelt Drivatars tijdens vrij rijden in. Hiervoor moet je het spel opnieuw starten. Tegenstanders in races en evenementbeloningen blijven ongewijzigd.",
    .fov_label = "Gezichtsveld",
    .skill_counter_shake = "Trillende vaardigheidsteller",
    .skill_counter_shake_description = "Schakelt het trillen en vergroten van de vaardigheidsketenteller in.",
    .speed_zone_vignette = "Vignet in snelheidszones",
    .speed_zone_vignette_description = "Schakelt het extra vignet in snelheidszones in.",
    .skill_point_notification = "Melding vaardigheidspunt verdiend",
    .skill_point_description = "Toont de melding voor een verdiend vaardigheidspunt en speelt de bijbehorende dialoog af. Punten worden ook toegekend als deze optie uitstaat.",
    .unit_names = {"Vermogenseenheden", "Veereenheden"},
    .unit_descriptions = {"Kies de eenheid voor het motorvermogen. Standaard volgt de instelling voor eenheden.", "Kies de eenheid voor de veerstijfheid. Standaard volgt de instelling voor eenheden."},
    .master_volume = "Hoofdvolume",
    .manufacturer_jump = L"Naar fabrikant gaan",
    .event_labels = {L"Aantal Drivatars", L"Startvolgorde", L"Alleen standaardauto’s", L"Verkeer"},
    .grid_choices = {L"Willekeurig", L"Laagste PI eerst", L"Hoogste PI eerst"},
    .division_label = L"Autodivisie",
    .garage_label = L"Terug naar garage",
    .garage_question = L"Terug naar de festivalgarage op de huidige kaart?",
    .camera = "Driftcamera",
    .driver = "Bestuurder",
    .parameters = {"Gevoeligheid", "Kijksnelheid", "Bereik"},
    .descriptions = {"Schakel de driftcamera in voor volg- en cockpitbeeld, alleen voor "
                     "cockpitbeeld, of schakel deze uit.",
                     "De verhouding tussen de hoek van de auto en die van de camera. "
                     "Verhoog "
                     "deze instelling om de "
                     "camera gevoeliger te maken voor de hoek van de auto.",
                     "De snelheid waarmee de camera vanuit de oorspronkelijke positie naar "
                     "de "
                     "bewegingsrichting "
                     "draait. Een lagere instelling maakt de camerabeweging bij "
                     "richtingsveranderingen vloeiender.",
                     "Verlaag deze instelling om de camerabeweging te beperken wanneer de "
                     "auto "
                     "zijn maximale hoek "
                     "bereikt."},
    .choices = {L"Aan", L"Bestuurder", L"Uit"},
    .fov_views = {"Volgen", "Ver volgen", "Bestuurder", "Motorkap", "Bumper"},
    .fov_description = "Stel het verticale gezichtsveld van de betreffende "
                       "camera in graden in.",
    .settings_names = {"Realtime schaduwen", "", "Samples fotomodus", "", "Oriëntatie minikaart",
                       "Filmische camera Autodrive", "Kampioenschapsdialoog overslaan",
                       "Startlocatie", "Positie bij autowissel", "Bestickering in 4K",
                       "Verbeterde bestickerings-MSAA", "Verbeterde bestickeringssampling",
                       "Alle evenementen tonen", "Autodrive in evenementen", "Eigen showcaseauto",
                       "Bens fotomodusdialoog", "Ashleys garagedialoog", "Wheelspinmelding",
                       "Geluid bij controlepunten"},
    .settings_descriptions =
        {"Regel vloeiende zonneschaduwen. Standaard volgt de instelling van de huidige vertaallaag. Herstart vereist.",
         "",
         "Stel fotomonsters in. Standaard volgt de instelling van de huidige vertaallaag. Herstart vereist.",
         "",
         "Kies of de minikaart de auto of de camera volgt.",
         "Gebruikt filmische camera’s tijdens Autodrive.",
         "Slaat de verplichte dialoog over bij een kampioenschapsrace met een ongeschikte auto. Opnieuw starten vereist.",
         "Kies de startlocatie bij het opstarten van het spel. Standaard start je zoals gewoonlijk bij de autobijeenkomst het dichtst bij je positie toen je het spel afsloot. Start het spel opnieuw om wijzigingen toe te passen.",
         "Waar de nieuwe auto moet staan wanneer je in de open wereld van auto wisselt. De tweede optie laat je precies staan waar de vorige auto was.",
         "Gebruik 4K-bestickering. Standaard volgt de vertaallaag. Herstart vereist.",
         "Gebruik 4x MSAA. Standaard volgt de vertaallaag. Herstart vereist.",
         "Gebruik 2x sampling. Standaard volgt de vertaallaag. Herstart vereist.",
         "Toont alle punten voor race-instellingen op de kaart. Hiermee kun je elke race in het spel starten.",
         "Sta Autodrive toe in races, roadtrips en andere evenementen met een route.",
         "Kies een eigen auto voor showcase-evenementen.",
         "Speelt Bens dialoog af als een foto geen voor het eerst gefotografeerde auto's bevat.",
         "Speelt Ashleys terugkerende begroetingen af wanneer je de garage binnengaat.",
         "Toont de wheelspinmelding tijdens vrij rijden en speelt de bijbehorende dialoog af. Berichten na races en wheelspinbeloningen blijven actief als deze optie uitstaat.",
         "Speelt het geluidseffect af wanneer je een controlepunt passeert."},
    .default_choice = L"Standaard",
    .minimap_choices = {L"Auto volgen", L"Camera volgen"},
    .spawn_choices = {L"Ingang autobijeenkomst", L"Festivalterrein"},
    .car_position_choices = {L"Dichtstbijzijnde weg", L"Dezelfde locatie"},
    .keyboard_mouse = "TOETSENBORD EN MUIS",
    .kbm_names = {"Invoermodus", "Rondkijken met muis", "Rondkijken omhoog/omlaag", "Muis-Y-as omkeren", "Muisgevoeligheid", "Stuursnelheid toetsenbord", "Menuaanwijzer", "Cursorgrootte", "Kaartcursorsnelheid"},
    .kbm_descriptions =
        {"Auto schakelt naar wat je het laatst hebt gebruikt. Toetsenbord en muis of Controller houden één bron. Direct actief.",
         "Tijdens het rijden draait de muis de camera en keert het beeld na een moment terug naar het midden. Direct actief.",
         "In de bumper-, motorkap-, cockpit- en bestuurderscamera kantelt het vooruit en achteruit bewegen van de muis het beeld omhoog en omlaag; het keert terug zoals het zijwaarts rondkijken. Direct actief.",
         "Draait omhoog en omlaag om voor de muis: omhoog en omlaag kijken tijdens het rijden, de fotomoduscamera en draaien in de autoviewer. De kaartcursor verandert niet. Direct actief.",
         "Hoe ver het beeld draait per muisbeweging. Direct actief.",
         "Hoe snel de stuurtoetsen het stuur draaien en laten terugkeren. Direct actief.",
         "Een aanwijzer in het pauzemenu en op de kaart: klik om te kiezen, rechtsklik om terug te gaan, scrollwiel om te scrollen. Direct actief.",
         "Grootte van de menuaanwijzer. Direct actief.",
         "Hoe snel de muis de cursor op de wereldkaart beweegt. Direct actief."},
    .kbm_modes = {L"Auto", L"Toetsenbord en muis", L"Controller"},
    .kbm_actions = {"Gas", "Rem", "Links sturen", "Rechts sturen", "Handrem", "Opschakelen", "Terugschakelen", "Koppeling", "Camera wisselen", "Terugspoelen", "Achterom kijken", "Activeren", "Fotomodus", "Vorige zender", "Volgende zender", "Kaart", "Pauze", "Menu: bevestigen", "Menu: terug", "Menu: omhoog", "Menu: omlaag", "Menu: links", "Menu: rechts", "Menu: vorig tabblad", "Menu: volgend tabblad", "Menu: X-knop", "Menu: Y-knop", "Menu: linkertrigger", "Menu: rechtertrigger", "Menu: D-pad omhoog", "Menu: D-pad omlaag", "Menu: D-pad links", "Menu: D-pad rechts"},
    .kbm_key_descriptions =
        {"Toets voor deze rijactie. Druk op Enter (of links/rechts) op de regel en daarna op de nieuwe toets; Esc annuleert, Backspace of Delete wist hem. Een toets die een andere rijactie gebruikt, wordt geweigerd. Direct actief.",
         "Toets voor deze menuactie. Druk op Enter (of links/rechts) op de regel en daarna op de nieuwe toets; Esc annuleert, Backspace of Delete wist hem. Een toets die een andere menuactie gebruikt, wordt geweigerd. Direct actief."},
    .kbm_reset = "Toetsen herstellen",
    .kbm_reset_description = "Zet alle toetstoewijzingen terug naar de standaard. Druk op Enter of links/rechts. Direct actief.",
    .kbm_labels = {L"Druk op een toets...", L"Geen", L"Toets niet beschikbaar", L"Toetsenbord en muis uit", L"Opslaan mislukt", L"In gebruik", L"Herstellen", L"Klaar"}};

static const struct {
    const char *code;
    const VideoOptionsText *text;
} translations[] = {{"EN", &english},   {"GB", &english}, {"BR", &brazilian}, {"DE", &german},
                    {"FR", &french},    {"ES", &spanish}, {"MX", &mexican},   {"IT", &italian},
                    {"JP", &japanese},  {"KO", &korean},  {"CHT", &chinese},  {"CZ", &czech},
                    {"HU", &hungarian}, {"PL", &polish},  {"RU", &russian},   {"NL", &dutch}};

const VideoOptionsText *video_options_text(unsigned char *game_image) {
    void *manager;
    unsigned char code[32];
    size_t length, capacity;
    if (!game_image || !memory_read(game_image + GAME_RVA_TEXT_TABLES, &manager, sizeof(manager)) ||
        !manager || !memory_read((unsigned char *)manager + 0x30, code, sizeof(code))) {
        return &english;
    }
    memory_copy(&length, code + 16, sizeof(length));
    memory_copy(&capacity, code + 24, sizeof(capacity));
    if (capacity != 15 || length < 2 || length > 3 || code[length] != 0) {
        return &english;
    }
    for (unsigned index = 0; index < sizeof(translations) / sizeof(translations[0]); ++index) {
        if (text_equal_ignore_case((const char *)code, translations[index].code)) {
            return translations[index].text;
        }
    }
    return &english;
}

#ifdef FH2_TWEAKS_TEST
/* Offline: the index of the first translation missing a keyboard & mouse
   page text, or the number of translations when none is. */
__declspec(dllexport) unsigned Fh2TestKeyboardTextComplete(void) {
    for (unsigned index = 0; index < sizeof(translations) / sizeof(translations[0]); ++index) {
        const VideoOptionsText *text = translations[index].text;
        int complete = text->keyboard_mouse && text->kbm_reset && text->kbm_reset_description;
        for (unsigned i = 0; i < KBM_TEXT_ROWS; ++i) {
            complete &= text->kbm_names[i] && text->kbm_descriptions[i];
        }
        for (unsigned i = 0; i < KBM_TEXT_ACTIONS; ++i) {
            complete &= text->kbm_actions[i] != NULL;
        }
        for (unsigned i = 0; i < 3; ++i) {
            complete &= text->kbm_modes[i] != NULL;
        }
        for (unsigned i = 0; i < 2; ++i) {
            complete &= text->kbm_key_descriptions[i] != NULL;
        }
        for (unsigned i = 0; i < KBM_LABEL_COUNT; ++i) {
            complete &= text->kbm_labels[i] != NULL;
        }
        if (!complete) {
            return index;
        }
    }
    return sizeof(translations) / sizeof(translations[0]);
}
#endif
