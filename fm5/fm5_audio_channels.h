/* Native MasterBank mixer groups. Identity is read through the bus description getID interface. */
static const Channel channels[FM5_AUDIO_COUNT]={
    {"EoAudioMusic","MUSIC VOLUME","Music Volume",{0x402b72beb934cbe9ULL,0x2adb8ac20b6fccadULL}}, /* bus:/master/mastersubgroup/m_music */
    {"EoAudioVoice","VOICEOVER VOLUME","Voiceover Volume",{0x4f8b4c6b175e3b97ULL,0x8b3f857f61774e86ULL}}, /* bus:/master/mastersubgroup/m_vo */
    {"EoAudioCar","CAR VOLUME","Car Volume",{0x4319480beada52b7ULL,0xbfaba417d0f916aaULL}}, /* bus:/master/mastersubgroup/m_car */
    {"EoAudioTyres","TYRES VOLUME","Tyres Volume",{0x436905844eb10e34ULL,0xddbe1f5c39e2149dULL}}, /* bus:/master/mastersubgroup/m_tires */
    {"EoAudioCollisions","COLLISION VOLUME","Collision Volume",{0x43efa79b529c79acULL,0xc8a9b85c3fda62abULL}}, /* bus:/master/mastersubgroup/m_collisions */
    {"EoAudioCockpit","COCKPIT SOUNDS VOLUME","Cockpit Sounds Volume",{0x43251c7650f95fc1ULL,0x585f4619d0cfada3ULL}}, /* bus:/master/mastersubgroup/m_cockpit */
    {"EoAudioTrack","TRACK AMBIENCE VOLUME","Track Ambience Volume",{0x4144f21b538a4481ULL,0x36de984dedc912a6ULL}}, /* bus:/master/mastersubgroup/m_tracks */
    {"EoAudioCrowd","CROWD VOLUME","Crowd Volume",{0x420358a2e3c5ff1cULL,0x40a0ff9ad5b59892ULL}}, /* bus:/master/mastersubgroup/m_tracks/m_crowds */
    {"EoAudioGarage","GARAGE AMBIENCE VOLUME","Garage Ambience Volume",{0x45e62b0cf753113dULL,0xf9c81b9bff81a682ULL}}, /* bus:/master/mastersubgroup/m_homespaceamb */
    {"EoAudioObjects","WORLD OBJECTS VOLUME","World Objects Volume",{0x453c52c93a848d53ULL,0xfb0298fe3dc43887ULL}}, /* bus:/master/mastersubgroup/m_sos */
    {"EoAudioReverb","ENVIRONMENT REVERB VOLUME","Environment Reverb Volume",{0x40afe7d1c5045a8aULL,0x4dbb9f1933c9b394ULL}}, /* bus:/master/mastersubgroup/m_environment */
    {"EoAudioWind","WIND VOLUME","Wind Volume",{0x477e9eac9b93ae12ULL,0xa132a3a4421bf7aeULL}}, /* bus:/master/mastersubgroup/m_wind */
    {"EoAudioInterface","INTERFACE SOUNDS VOLUME","Interface Sounds Volume",{0x43d93d4c4e220364ULL,0x537958cb035179acULL}}, /* bus:/master/m_ui */
    {"EoAudioVista","FORZAVISTA EFFECTS VOLUME","ForzaVista Effects Volume",{0x4a33651bf32a4dfaULL,0xde20e78dfbb52696ULL}}, /* bus:/master/mastersubgroup/m_autovista */
};
