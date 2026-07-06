graph TD
    %% Core Navigation
    NORMAL -- "tap anywhere" --> HOME
    HOME -- "0-2 (BACK)" --> NORMAL
    HOME -- "3" --> TRACKPAD
    HOME -- "4" --> SETTINGS
    HOME -- "5" --> HUB

    TRACKPAD -- "exit (top-left tap)" --> HOME

    %% Settings (3x3 layout)
    SETTINGS -- "1 (back)" --> HOME
    SETTINGS -. "0" .-> S_SensP[sens+]
    SETTINGS -. "2" .-> S_BriP[bright+]
    SETTINGS -. "3" .-> S_SensM[sens-]
    SETTINGS -. "4" .-> S_Rot[rotate 180deg]
    SETTINGS -. "5" .-> S_BriM[bright-]
    SETTINGS -. "6 (blue)" .-> S_SensR[sens readout <br> GPS icon + 0..10]
    SETTINGS -. "8 (blue)" .-> S_BriR[bright readout <br> eye icon + %]

    %% Hub Sub-menus
    HUB -- "1 (back)" --> HOME
    HUB -- "0" --> FKEYS
    HUB -- "2" --> NUMPAD
    HUB -- "3" --> SYMBOLS
    HUB -- "4" --> MEDIA
    HUB -- "5" --> MODIFIERS

    %% Media Actions
    MEDIA -- "1 (back)" --> HUB
    MEDIA -. "0" .-> M_VolM[vol-]
    MEDIA -. "2" .-> M_VolP[vol+]
    MEDIA -. "3" .-> M_Prev[prev]
    MEDIA -. "4" .-> M_Play[play/pause]
    MEDIA -. "5" .-> M_Next[next]

    %% Paginated Menus (FKeys / Symbols - 3x3, 7 keys/page)
    FKEYS -- "cell 1 (pg0)" --> HUB
    FKEYS -. "cell 1 (>pg0)" .-> FK_Prev[Prev Page]
    FKEYS -. "cell 7" .-> FK_Next[Next Page]

    SYMBOLS -- "cell 1 (pg0)" --> HUB
    SYMBOLS -. "cell 1 (>pg0)" .-> SYM_Prev[Prev Page]
    SYMBOLS -. "cell 7" .-> SYM_Next[Next Page]

    %% Numpad (4x4 layout)
    NUMPAD -- "back" --> HUB
    NUMPAD -. "inputs" .-> N_Keys["7 8 9 +<br>4 5 6 -<br>1 2 3 *<br>0 enter /<br>(operators blue)"]

    %% Modifiers
    MODIFIERS -. "actions" .-> MOD_Keys["One-shot: Ctrl / Shift / Alt / Gui"]
    MODIFIERS -. "armed state" .-> MOD_State["solid blue fill + black text"]
