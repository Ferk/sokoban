(() => {
    const app = window.SokobanWebApp = window.SokobanWebApp || {};

    function normalizeBuiltInPacks(configuredPacks) {
        if (!Array.isArray(configuredPacks)) {
            return [];
        }

        return configuredPacks.flatMap(pack => {
            if (pack === null || typeof pack !== 'object') {
                return [];
            }

            const url = typeof pack.url === 'string' ? pack.url.trim() : '';
            if (url === '') {
                return [];
            }

            const label = typeof pack.label === 'string' && pack.label.trim() !== ''
                ? pack.label.trim()
                : (url.split('/').pop() || url);
            const description = typeof pack.description === 'string' ? pack.description : '';

            return [{ label, url, description }];
        });
    }

    app.constants = {
        eventTickTime: 100,
        successDialogDelayMs: 1000,
        successAdvancePromptDelayMs: 1000,
        cameraEdgeMarginRatio: 0.2,
    };

    app.elements = {
        pageTitle: document.getElementById('page-title'),
        source: document.getElementById('source'),
        status: document.getElementById('status'),
        undoButton: document.getElementById('undo-button'),
        levelStats: document.getElementById('level-stats'),
        headerCopy: document.getElementById('header-copy'),
        helpButton: document.getElementById('help-button'),
        helpDialog: document.getElementById('help-dialog'),
        packButton: document.getElementById('pack-button'),
        packDialog: document.getElementById('pack-dialog'),
        packDialogClose: document.getElementById('pack-dialog-close'),
        packUrlForm: document.getElementById('pack-url-form'),
        packUrlInput: document.getElementById('pack-url-input'),
        packDropzone: document.getElementById('pack-dropzone'),
        packFileInput: document.getElementById('pack-file-input'),
        packBuiltInSection: document.getElementById('pack-builtins-section'),
        packList: document.getElementById('pack-list'),
        packDialogMessage: document.getElementById('pack-dialog-message'),
        packErrorCallout: document.getElementById('pack-error-callout'),
        packErrorDetail: document.getElementById('pack-error-detail'),
        infoButton: document.getElementById('info-button'),
        infoDialog: document.getElementById('info-dialog'),
        infoDialogTitle: document.getElementById('info-dialog-title'),
        infoContent: document.getElementById('info-content'),
        successDialog: document.getElementById('success-dialog'),
        successDialogTitle: document.getElementById('success-dialog-title'),
        successTime: document.getElementById('success-time'),
        successMoves: document.getElementById('success-moves'),
        successMessage: document.getElementById('success-message'),
        boardViewport: document.getElementById('board-viewport'),
        board: document.getElementById('game-board'),
    };

    app.runtime = {};

    app.state = {
        packText: '',
        packLabel: 'Loading pack...',
        packUrl: '',
        currentLevelIndex: 0,
        totalLevels: 1,
        gameReady: false,
        loadingLevel: false,
        levelTimerInterval: null,
        successDialogTimer: null,
        successPromptTimer: null,
        currentPlayerTile: null,
        currentLevelTitle: '',
        currentLevelDescription: '',
        currentPackMetadata: '',
        currentMoveCount: 0,
        firstMoveTimestamp: null,
        awaitingAdvanceAfterWin: false,
        gameplayFocusTimer: null,
        queuedAutoMoves: '',
        autoMoveTimer: null,
        undoRepeatTimer: null,
        undoRepeatPointerId: null,
        undoRepeatSuppressClick: false,
        lastUndoPointerDownAt: 0,
    };

    app.core = {
        builtInPacks: normalizeBuiltInPacks(window.SokobanWebConfig?.builtInPacks),

        bindRuntimeApi() {
            app.runtime.initLevel = Module.cwrap('sokoban_init_web_level', 'boolean', ['string', 'number']);
            app.runtime.countLevels = Module.cwrap('sokoban_count_levels_web', 'number', ['string']);
            app.runtime.getLevelTitle = Module.cwrap('sokoban_get_level_title_web', 'string', []);
            app.runtime.getLevelDescription = Module.cwrap('sokoban_get_level_description_web', 'string', []);
            app.runtime.getPackMetadata = Module.cwrap('sokoban_get_pack_metadata_web', 'string', []);
            app.runtime.handleInput = Module.cwrap('sokoban_handle_input', 'boolean', ['number']);
            app.runtime.planTapPath = Module.cwrap('sokoban_plan_tap_path_web', 'string', ['number', 'number']);
            app.runtime.isEventOngoing = Module.cwrap('sokoban_is_event_ongoing', 'boolean', []);
            app.runtime.processEvent = Module.cwrap('sokoban_process_event', 'boolean', []);
            app.runtime.getRows = Module.cwrap('sokoban_get_rows', 'number', []);
            app.runtime.getCols = Module.cwrap('sokoban_get_cols', 'number', []);
            app.runtime.getMoveCount = Module.cwrap('sokoban_get_move_count_web', 'number', []);
            app.runtime.getTile = Module.cwrap('sokoban_get_tile', 'number', ['number', 'number']);
            app.runtime.getInitialRows = Module.cwrap('sokoban_get_initial_rows_web', 'number', []);
            app.runtime.getInitialCols = Module.cwrap('sokoban_get_initial_cols_web', 'number', []);
            app.runtime.getInitialTile = Module.cwrap('sokoban_get_initial_tile_web', 'number', ['number', 'number']);
            app.runtime.isGameWon = Module.cwrap('sokoban_is_game_won', 'boolean', []);
        },

        applyPackToUrl(levelUrl) {
            const nextUrl = new URL(window.location.href);

            nextUrl.searchParams.set('l', levelUrl);
            window.location.assign(nextUrl.toString());
        },

        findBuiltInPackByUrl(levelUrl) {
            return app.core.builtInPacks.find(pack => pack.url === levelUrl) || null;
        },

        async fetchPackFromUrl(levelUrl, labelHint = '') {
            const resolvedUrl = new URL(levelUrl, window.location.href);
            const response = await fetch(resolvedUrl);

            if (!response.ok) {
                throw new Error(`Could not load ${resolvedUrl.href} (HTTP ${response.status}).`);
            }

            return {
                text: await response.text(),
                label: labelHint || resolvedUrl.pathname.split('/').pop() || resolvedUrl.href,
                url: resolvedUrl.href
            };
        },

        async activatePack(loadedPack, clearUrlParam = false) {
            const { state } = app;
            const detectedLevels = app.runtime.countLevels(loadedPack.text);

            if (detectedLevels <= 0) {
                throw new Error('No valid Sokoban levels were found.');
            }

            state.packText = loadedPack.text;
            state.packLabel = loadedPack.label;
            state.packUrl = loadedPack.url;
            state.totalLevels = detectedLevels;
            state.currentLevelIndex = 0;

            if (clearUrlParam) {
                const nextUrl = new URL(window.location.href);

                nextUrl.searchParams.delete('l');
                window.history.replaceState(null, '', nextUrl.toString());
            }

            if (!app.core.loadLevel(0)) {
                throw new Error('The first level could not be loaded.');
            }
        },

        async loadLocalPackFile(file) {
            const { elements, state } = app;

            if (!file) {
                return;
            }

            try {
                app.ui.setPackDialogMessage('Loading local file...');
                await app.core.activatePack(
                    {
                        text: await file.text(),
                        label: file.name,
                        url: ''
                    },
                    true
                );
                elements.packDialog.close();
                app.ui.setPackDialogMessage('');
            } catch (error) {
                app.ui.setPackDialogMessage(error.message);
                if (!state.gameReady) {
                    app.ui.showPackErrorCallout(error.message);
                }
            }
        },

        loadLevel(levelIndex) {
            const { state } = app;

            app.ui.resetLevelStats();
            state.loadingLevel = true;
            state.currentLevelIndex = levelIndex;
            app.ui.refreshHeader();
            app.ui.refreshStatus();

            if (!app.runtime.initLevel(state.packText, levelIndex)) {
                state.loadingLevel = false;
                state.gameReady = false;
                app.ui.clearCurrentLevelInfo();
                app.ui.refreshLevelInfo();
                app.ui.showPackErrorCallout(`The current pack could not load level ${levelIndex + 1}.`);
                app.ui.setStatus(`Could not load level ${levelIndex + 1}.`, 'error');
                return false;
            }

            app.ui.syncCurrentLevelInfoFromRuntime();
            state.loadingLevel = false;
            state.gameReady = true;
            app.ui.refreshLevelInfo();
            app.ui.hidePackErrorCallout();
            app.board.drawBoard(true);
            app.ui.scheduleGameplayFocusRestore();
            return true;
        },

        async loadRequestedPack() {
            const levelParam = new URLSearchParams(window.location.search).get('l');

            if (levelParam === null) {
                if (app.core.builtInPacks.length === 0) {
                    return null;
                }
                return app.core.fetchPackFromUrl(app.core.builtInPacks[0].url, app.core.builtInPacks[0].label);
            }
            if (levelParam.trim() === '') {
                throw new Error("The 'l' query parameter is empty.");
            }

            const builtInPack = app.core.findBuiltInPackByUrl(levelParam);
            return app.core.fetchPackFromUrl(levelParam, builtInPack ? builtInPack.label : '');
        },

        async initGame() {
            const { elements, state } = app;

            try {
                app.ui.setStatus('Loading levels...');
                app.ui.resetLevelStats();
                app.ui.hidePackErrorCallout();

                const requestedPack = await app.core.loadRequestedPack();
                if (requestedPack === null) {
                    app.ui.showPackSelectionState();
                    return;
                }

                await app.core.activatePack(requestedPack);
            } catch (error) {
                elements.board.innerHTML = '';
                elements.source.textContent = '';
                elements.pageTitle.textContent = 'Load error';
                state.gameReady = false;
                app.ui.clearCurrentLevelInfo();
                app.ui.resetLevelStats();
                app.ui.refreshLevelInfo();
                app.ui.showPackErrorCallout(error.message);
                app.ui.setStatus(error.message, 'error');
                console.error(error);
            }
        },
    };
})();
