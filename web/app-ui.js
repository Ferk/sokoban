(() => {
    const app = window.SokobanWebApp;
    const { constants, elements, state } = app;

    function buildInfoTextBlock(text, emptyText) {
        const paragraph = document.createElement('p');
        const content = text.trim();

        paragraph.className = 'info-text-block';
        if (content === '') {
            paragraph.classList.add('is-empty');
            paragraph.textContent = emptyText;
        } else {
            paragraph.textContent = content;
        }
        return paragraph;
    }

    app.ui = {
        clearCurrentLevelInfo() {
            state.currentLevelTitle = '';
            state.currentLevelDescription = '';
            state.currentPackMetadata = '';
        },

        syncCurrentLevelInfoFromRuntime() {
            state.currentLevelTitle = app.runtime.getLevelTitle();
            state.currentLevelDescription = app.runtime.getLevelDescription();
            state.currentPackMetadata = app.runtime.getPackMetadata();
        },

        syncMoveCountFromRuntime() {
            state.currentMoveCount = app.runtime.getMoveCount();
        },

        setStatus(message, tone = '') {
            elements.status.textContent = message;
            elements.status.title = message;
            elements.status.className = tone;
        },

        formatElapsedTime(elapsedMs) {
            const totalSeconds = Math.max(0, Math.floor(elapsedMs / 1000));
            const hours = Math.floor(totalSeconds / 3600);
            const minutes = Math.floor((totalSeconds % 3600) / 60);
            const seconds = totalSeconds % 60;

            if (hours > 0) {
                return `${hours}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
            }

            return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
        },

        formatMoveCount(moveCount) {
            return `${moveCount} move${moveCount === 1 ? '' : 's'}`;
        },

        refreshLevelStats() {
            if (state.firstMoveTimestamp === null) {
                elements.undoButton.hidden = true;
                elements.levelStats.hidden = true;
                elements.levelStats.textContent = '';
                elements.levelStats.title = '';
                return;
            }

            const elapsedTime = app.ui.formatElapsedTime(Date.now() - state.firstMoveTimestamp);
            const moveLabel = app.ui.formatMoveCount(state.currentMoveCount);

            elements.undoButton.hidden = false;
            elements.levelStats.hidden = false;
            elements.levelStats.textContent = `${elapsedTime} · ${moveLabel}`;
            elements.levelStats.title = `Time ${elapsedTime}, ${moveLabel}`;
        },

        refreshHeader() {
            const levelLabel = state.totalLevels > 1 ? `Level ${state.currentLevelIndex + 1} / ${state.totalLevels}` : 'Single level';

            elements.pageTitle.textContent = state.packLabel;
            elements.source.textContent = levelLabel;
            elements.source.title = state.packUrl;
        },

        refreshLevelInfo() {
            const hasInfo = state.gameReady;
            const dialogTitle = state.totalLevels > 1
                ? `${state.packLabel} - Level ${state.currentLevelIndex + 1} information`
                : `${state.packLabel} information`;
            const levelTitle = state.currentLevelTitle.trim() !== ''
                ? state.currentLevelTitle.trim()
                : (state.totalLevels > 1 ? `Level ${state.currentLevelIndex + 1}` : state.packLabel);
            const contentFragment = document.createDocumentFragment();
            const titleElement = document.createElement('h3');
            const separatorElement = document.createElement('div');

            elements.infoButton.hidden = !hasInfo;
            elements.headerCopy.dataset.clickable = hasInfo ? 'true' : 'false';
            elements.headerCopy.setAttribute('aria-disabled', hasInfo ? 'false' : 'true');
            elements.headerCopy.tabIndex = hasInfo ? 0 : -1;
            elements.infoDialogTitle.textContent = dialogTitle;
            elements.infoContent.replaceChildren();

            if (!hasInfo) {
                if (elements.infoDialog.open) {
                    elements.infoDialog.close();
                }
                return;
            }

            titleElement.className = 'info-level-title';
            titleElement.textContent = levelTitle;
            separatorElement.className = 'info-divider';

            contentFragment.appendChild(titleElement);
            contentFragment.appendChild(buildInfoTextBlock(state.currentLevelDescription, 'No level description available.'));
            contentFragment.appendChild(app.board.buildInfoBoardPreview());
            contentFragment.appendChild(separatorElement);
            contentFragment.appendChild(buildInfoTextBlock(state.currentPackMetadata, 'No level pack information available.'));
            elements.infoContent.appendChild(contentFragment);
        },

        refreshStatus() {
            if (!state.gameReady) {
                return;
            }
            if (state.loadingLevel) {
                app.ui.setStatus(`Loading level ${state.currentLevelIndex + 1}...`);
                return;
            }
            if (app.runtime.isGameWon()) {
                if (state.currentLevelIndex + 1 < state.totalLevels) {
                    app.ui.setStatus(`Level ${state.currentLevelIndex + 1} complete!`, 'success');
                } else {
                    app.ui.setStatus(`Congratulations! You completed all ${state.totalLevels} levels.`, 'success');
                }
                return;
            }
            if (state.totalLevels > 1) {
                app.ui.setStatus(`Ready. Level ${state.currentLevelIndex + 1} of ${state.totalLevels}.`);
            } else {
                app.ui.setStatus('Ready.');
            }
        },

        setPackDialogMessage(message = '') {
            elements.packDialogMessage.textContent = message;
        },

        showPackErrorCallout(message = '') {
            elements.packErrorDetail.textContent = message;
            elements.packErrorCallout.hidden = false;
        },

        hidePackErrorCallout() {
            elements.packErrorDetail.textContent = '';
            elements.packErrorCallout.hidden = true;
        },

        showPackSelectionState(message = 'Choose a level pack from a URL or local file to begin.') {
            state.packText = '';
            state.packLabel = 'Sokoban';
            state.packUrl = '';
            state.totalLevels = 1;
            state.currentLevelIndex = 0;
            state.loadingLevel = false;
            state.gameReady = false;
            state.currentPlayerTile = null;
            elements.board.innerHTML = '';
            elements.board.style.removeProperty('--cols');
            elements.pageTitle.textContent = state.packLabel;
            elements.source.textContent = '';
            app.ui.clearCurrentLevelInfo();
            app.ui.refreshLevelInfo();
            app.ui.hidePackErrorCallout();
            app.ui.setStatus(message);
        },

        closeDialogIfOpen(dialog) {
            if (dialog.open) {
                dialog.close();
            }
        },

        closeDialogOnBackdrop(dialog) {
            dialog.addEventListener('click', event => {
                if (event.target === dialog) {
                    dialog.close();
                }
            });
        },

        isModalDialogOpen() {
            return elements.infoDialog.open || elements.helpDialog.open || elements.packDialog.open || elements.successDialog.open;
        },

        clearSuccessDialogTimer() {
            if (state.successDialogTimer !== null) {
                window.clearTimeout(state.successDialogTimer);
                state.successDialogTimer = null;
            }
        },

        clearSuccessPromptTimer() {
            if (state.successPromptTimer !== null) {
                window.clearTimeout(state.successPromptTimer);
                state.successPromptTimer = null;
            }
        },

        clearAutoMoveTimer() {
            if (state.autoMoveTimer !== null) {
                window.clearTimeout(state.autoMoveTimer);
                state.autoMoveTimer = null;
            }
        },

        clearUndoRepeatTimer() {
            if (state.undoRepeatTimer !== null) {
                window.clearTimeout(state.undoRepeatTimer);
                state.undoRepeatTimer = null;
            }
        },

        stopUndoRepeat() {
            app.ui.clearUndoRepeatTimer();
            state.undoRepeatPointerId = null;
        },

        clearQueuedAutoMoves() {
            state.queuedAutoMoves = '';
            app.ui.clearAutoMoveTimer();
        },

        clearGameplayFocusTimer() {
            if (state.gameplayFocusTimer !== null) {
                window.clearTimeout(state.gameplayFocusTimer);
                state.gameplayFocusTimer = null;
            }
        },

        focusGameplaySurface() {
            app.ui.clearGameplayFocusTimer();
            if (document.hidden || !state.gameReady || state.loadingLevel || app.ui.isModalDialogOpen()) {
                return false;
            }

            try {
                elements.boardViewport.focus({ preventScroll: true });
            } catch (error) {
                elements.boardViewport.focus();
            }
            return true;
        },

        scheduleGameplayFocusRestore() {
            app.ui.clearGameplayFocusTimer();
            state.gameplayFocusTimer = window.setTimeout(() => {
                state.gameplayFocusTimer = null;
                app.ui.focusGameplaySurface();
            }, 0);
        },

        stopLevelTimer() {
            if (state.levelTimerInterval !== null) {
                window.clearInterval(state.levelTimerInterval);
                state.levelTimerInterval = null;
            }
        },

        startLevelStatsIfNeeded() {
            if (state.firstMoveTimestamp !== null) {
                return;
            }

            state.firstMoveTimestamp = Date.now();
            app.ui.refreshLevelStats();
            state.levelTimerInterval = window.setInterval(() => {
                app.ui.refreshLevelStats();
            }, 1000);
        },

        resetLevelStats() {
            state.currentMoveCount = 0;
            state.firstMoveTimestamp = null;
            state.awaitingAdvanceAfterWin = false;
            app.ui.clearQueuedAutoMoves();
            app.ui.stopUndoRepeat();
            app.ui.clearSuccessDialogTimer();
            app.ui.clearSuccessPromptTimer();
            app.ui.clearGameplayFocusTimer();
            app.ui.stopLevelTimer();
            app.ui.refreshLevelStats();
            elements.successMessage.dataset.visible = 'false';
            app.ui.closeDialogIfOpen(elements.successDialog);
        },

        showSuccessDialog() {
            const hasNextLevel = state.currentLevelIndex + 1 < state.totalLevels;
            const elapsedMs = state.firstMoveTimestamp === null ? 0 : Date.now() - state.firstMoveTimestamp;

            state.successDialogTimer = null;
            state.awaitingAdvanceAfterWin = false;
            app.ui.clearQueuedAutoMoves();
            app.ui.clearSuccessPromptTimer();
            app.ui.stopLevelTimer();
            if (state.firstMoveTimestamp !== null) {
                elements.levelStats.hidden = false;
                elements.levelStats.textContent = `${app.ui.formatElapsedTime(elapsedMs)} · ${app.ui.formatMoveCount(state.currentMoveCount)}`;
                elements.levelStats.title = `Time ${app.ui.formatElapsedTime(elapsedMs)}, ${app.ui.formatMoveCount(state.currentMoveCount)}`;
            }
            elements.successDialogTitle.textContent = hasNextLevel ? `Level ${state.currentLevelIndex + 1} Complete` : 'All Levels Complete';
            elements.successTime.textContent = app.ui.formatElapsedTime(elapsedMs);
            elements.successMoves.textContent = String(state.currentMoveCount);
            elements.successMessage.textContent = hasNextLevel ? 'Press any key to go to the next level.' : 'Press any key to close.';
            elements.successMessage.dataset.visible = 'false';
            app.ui.closeDialogIfOpen(elements.infoDialog);
            app.ui.closeDialogIfOpen(elements.helpDialog);
            app.ui.closeDialogIfOpen(elements.packDialog);
            if (!elements.successDialog.open) {
                elements.successDialog.showModal();
            }
            state.successPromptTimer = window.setTimeout(() => {
                state.successPromptTimer = null;
                state.awaitingAdvanceAfterWin = true;
                elements.successMessage.dataset.visible = 'true';
            }, constants.successAdvancePromptDelayMs);
        },

        scheduleSuccessDialog() {
            if (state.successDialogTimer !== null || elements.successDialog.open) {
                return;
            }

            state.successDialogTimer = window.setTimeout(() => {
                app.ui.showSuccessDialog();
            }, constants.successDialogDelayMs);
        },

        advanceAfterWin() {
            if (!state.awaitingAdvanceAfterWin) {
                return;
            }

            state.awaitingAdvanceAfterWin = false;
            app.ui.closeDialogIfOpen(elements.successDialog);
            if (state.currentLevelIndex + 1 < state.totalLevels) {
                app.core.loadLevel(state.currentLevelIndex + 1);
            }
        },

        handleSuccessDialogInteraction(event) {
            if (!elements.successDialog.open) {
                return;
            }
            if (!state.awaitingAdvanceAfterWin) {
                event.preventDefault();
                return;
            }

            event.preventDefault();
            app.ui.advanceAfterWin();
        },

        maybeAdvanceLevel() {
            if (!state.gameReady || !app.runtime.isGameWon()) {
                app.ui.refreshStatus();
                return;
            }
            if (state.awaitingAdvanceAfterWin || state.successDialogTimer !== null || elements.successDialog.open) {
                return;
            }

            app.ui.scheduleSuccessDialog();
        },

        isDirectionalInput(inputChar) {
            return inputChar === 'w' || inputChar === 'a' || inputChar === 's' || inputChar === 'd';
        },

        applySuccessfulInputEffects(inputChar) {
            if (inputChar === 'r') {
                app.ui.resetLevelStats();
            } else {
                if (app.ui.isDirectionalInput(inputChar)) {
                    app.ui.startLevelStatsIfNeeded();
                }
                app.ui.syncMoveCountFromRuntime();
                if (state.currentMoveCount === 0) {
                    app.ui.resetLevelStats();
                } else {
                    app.ui.refreshLevelStats();
                }
            }
            app.board.drawBoard();
        },

        scheduleQueuedAutoMove(delayMs = constants.eventTickTime) {
            if (state.queuedAutoMoves === '' || state.autoMoveTimer !== null) {
                return;
            }

            state.autoMoveTimer = window.setTimeout(() => {
                state.autoMoveTimer = null;
                app.ui.runQueuedAutoMove();
            }, delayMs);
        },

        continueGameplaySequence() {
            if (app.runtime.isEventOngoing()) {
                window.setTimeout(() => {
                    app.ui.handleEventProgression();
                }, constants.eventTickTime);
                return;
            }
            if (app.runtime.isGameWon()) {
                app.ui.clearQueuedAutoMoves();
                app.ui.maybeAdvanceLevel();
                return;
            }
            if (state.queuedAutoMoves !== '') {
                app.ui.scheduleQueuedAutoMove();
                return;
            }
            app.ui.maybeAdvanceLevel();
        },

        runQueuedAutoMove() {
            if (state.queuedAutoMoves === '') {
                app.ui.maybeAdvanceLevel();
                return;
            }
            if (!state.gameReady || state.loadingLevel || app.ui.isModalDialogOpen() || app.runtime.isEventOngoing() || app.runtime.isGameWon()) {
                app.ui.clearQueuedAutoMoves();
                return;
            }

            const inputChar = state.queuedAutoMoves[0];

            if (!app.runtime.handleInput(inputChar.charCodeAt(0))) {
                app.ui.clearQueuedAutoMoves();
                app.ui.refreshStatus();
                return;
            }

            state.queuedAutoMoves = state.queuedAutoMoves.slice(1);
            app.ui.applySuccessfulInputEffects(inputChar);
            app.ui.continueGameplaySequence();
        },

        queueAutoMoves(moveSequence) {
            if (moveSequence === '') {
                return false;
            }

            app.ui.clearAutoMoveTimer();
            state.queuedAutoMoves = moveSequence;
            if (!app.runtime.isEventOngoing()) {
                app.ui.runQueuedAutoMove();
            }
            return true;
        },

        handleEventProgression() {
            if (app.runtime.processEvent()) {
                app.board.drawBoard();
            }
            if (app.runtime.isEventOngoing()) {
                window.setTimeout(() => {
                    app.ui.handleEventProgression();
                }, constants.eventTickTime);
                return;
            }
            app.ui.continueGameplaySequence();
        },

        getInputCharForKey(key) {
            const normalizedKey = key.length === 1 ? key.toLowerCase() : key;

            switch (normalizedKey) {
                case 'ArrowUp':
                case 'w':
                    return 'w';
                case 'ArrowDown':
                case 's':
                    return 's';
                case 'ArrowLeft':
                case 'a':
                    return 'a';
                case 'ArrowRight':
                case 'd':
                    return 'd';
                case 'u':
                    return 'u';
                case 'r':
                    return 'r';
                default:
                    return '';
            }
        },

        tryHandleGameplayInput(inputChar) {
            if (inputChar === '') {
                return false;
            }
            if (!state.gameReady || state.loadingLevel || app.runtime.isEventOngoing() || app.runtime.isGameWon()) {
                return false;
            }

            app.ui.clearQueuedAutoMoves();
            if (app.runtime.handleInput(inputChar.charCodeAt(0))) {
                app.ui.applySuccessfulInputEffects(inputChar);
            }
            app.ui.continueGameplaySequence();
            return true;
        },

        scheduleUndoRepeatStep() {
            if (state.undoRepeatPointerId === null || state.undoRepeatTimer !== null) {
                return;
            }

            state.undoRepeatTimer = window.setTimeout(() => {
                state.undoRepeatTimer = null;
                if (!app.ui.tryHandleGameplayInput('u')) {
                    app.ui.stopUndoRepeat();
                    return;
                }
                app.ui.scheduleUndoRepeatStep();
            }, constants.eventTickTime);
        },

        handleKeyPress(event) {
            if (elements.successDialog.open) {
                if (event.metaKey || event.ctrlKey || event.altKey || event.key === 'Shift') {
                    return;
                }
                event.preventDefault();
                if (state.awaitingAdvanceAfterWin) {
                    app.ui.advanceAfterWin();
                }
                return;
            }

            const inputChar = app.ui.getInputCharForKey(event.key);

            if (inputChar === '') {
                return;
            }
            if (elements.infoDialog.open || elements.helpDialog.open || elements.packDialog.open) {
                return;
            }

            event.preventDefault();
            app.ui.tryHandleGameplayInput(inputChar);
        },

        handleUndoButtonClick(event) {
            if (state.undoRepeatSuppressClick) {
                state.undoRepeatSuppressClick = false;
                event.preventDefault();
                return;
            }

            event.preventDefault();
            if (elements.infoDialog.open || elements.helpDialog.open || elements.packDialog.open || elements.successDialog.open) {
                return;
            }

            if (app.ui.tryHandleGameplayInput('u')) {
                app.ui.scheduleGameplayFocusRestore();
            }
        },

        handleUndoButtonPointerDown(event) {
            const isPrimaryPointer = event.isPrimary !== false;
            const isActivationButton = event.pointerType === 'touch' || event.button === 0;
            const now = Date.now();
            const isRepeatPress = state.lastUndoPointerDownAt !== 0 && now - state.lastUndoPointerDownAt <= 350;

            if (!isPrimaryPointer || !isActivationButton) {
                return;
            }

            state.lastUndoPointerDownAt = isRepeatPress ? 0 : now;
            state.undoRepeatPointerId = event.pointerId;
            if (typeof elements.undoButton.setPointerCapture === 'function') {
                try {
                    elements.undoButton.setPointerCapture(event.pointerId);
                } catch (error) {
                    // Ignore capture failures and continue with normal pointer tracking.
                }
            }

            if (!isRepeatPress) {
                return;
            }

            event.preventDefault();
            state.undoRepeatSuppressClick = true;
            if (!app.ui.tryHandleGameplayInput('u')) {
                app.ui.stopUndoRepeat();
                return;
            }
            app.ui.scheduleGameplayFocusRestore();
            app.ui.scheduleUndoRepeatStep();
        },

        handleUndoButtonPointerEnd(event) {
            if (state.undoRepeatPointerId !== null && event.pointerId !== state.undoRepeatPointerId) {
                return;
            }

            if (typeof elements.undoButton.releasePointerCapture === 'function' && state.undoRepeatPointerId !== null) {
                try {
                    elements.undoButton.releasePointerCapture(state.undoRepeatPointerId);
                } catch (error) {
                    // Ignore release failures if capture was never established.
                }
            }

            app.ui.stopUndoRepeat();
        },

        openPackDialog() {
            app.ui.clearQueuedAutoMoves();
            app.ui.closeDialogIfOpen(elements.infoDialog);
            app.ui.closeDialogIfOpen(elements.helpDialog);
            app.ui.setPackDialogMessage('');
            elements.packUrlInput.value = new URL(window.location.href).searchParams.get('l') || '';
            elements.packDialog.showModal();
            elements.packUrlInput.focus();
        },

        openHelpDialog() {
            app.ui.clearQueuedAutoMoves();
            app.ui.closeDialogIfOpen(elements.packDialog);
            app.ui.closeDialogIfOpen(elements.infoDialog);
            elements.helpDialog.showModal();
        },

        openInfoDialog() {
            if (elements.infoButton.hidden) {
                return;
            }

            app.ui.clearQueuedAutoMoves();
            app.ui.closeDialogIfOpen(elements.packDialog);
            app.ui.closeDialogIfOpen(elements.helpDialog);
            elements.infoDialog.showModal();
        },

        refreshBuiltInPackList() {
            const fragment = document.createDocumentFragment();

            if (elements.packList === null) {
                return;
            }

            elements.packList.replaceChildren();
            if (elements.packBuiltInSection !== null) {
                elements.packBuiltInSection.hidden = app.core.builtInPacks.length === 0;
            }
            if (app.core.builtInPacks.length === 0) {
                return;
            }

            app.core.builtInPacks.forEach(pack => {
                const button = document.createElement('button');

                button.type = 'button';
                button.className = 'pack-list-button';
                button.innerHTML = `<strong>${pack.label}</strong><span>${pack.description}</span>`;
                button.addEventListener('click', () => {
                    app.core.applyPackToUrl(pack.url);
                });
                fragment.appendChild(button);
            });
            elements.packList.appendChild(fragment);
        },

        initializePackHandlers() {
            elements.packButton.addEventListener('click', app.ui.openPackDialog);
            elements.packDialogClose.addEventListener('click', () => {
                app.ui.closeDialogIfOpen(elements.packDialog);
            });
            elements.packUrlForm.addEventListener('submit', event => {
                event.preventDefault();
                if (elements.packUrlInput.value.trim() === '') {
                    app.ui.setPackDialogMessage('Enter a level pack URL first.');
                    return;
                }
                app.core.applyPackToUrl(elements.packUrlInput.value.trim());
            });
            elements.packDropzone.addEventListener('dragover', event => {
                event.preventDefault();
                elements.packDropzone.classList.add('dragover');
            });
            elements.packDropzone.addEventListener('dragleave', () => {
                elements.packDropzone.classList.remove('dragover');
            });
            elements.packDropzone.addEventListener('drop', async event => {
                const [file] = event.dataTransfer.files;

                event.preventDefault();
                elements.packDropzone.classList.remove('dragover');
                await app.core.loadLocalPackFile(file);
            });
            elements.packFileInput.addEventListener('change', async event => {
                const [file] = event.target.files;

                try {
                    await app.core.loadLocalPackFile(file);
                } finally {
                    elements.packFileInput.value = '';
                }
            });
            elements.packErrorCallout.addEventListener('click', app.ui.openPackDialog);
            elements.packErrorCallout.addEventListener('keydown', event => {
                if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    app.ui.openPackDialog();
                }
            });
        },

        initializeDialogHandlers() {
            elements.helpButton.addEventListener('click', app.ui.openHelpDialog);
            elements.infoButton.addEventListener('click', app.ui.openInfoDialog);
            elements.headerCopy.addEventListener('click', app.ui.openInfoDialog);
            elements.headerCopy.addEventListener('keydown', event => {
                if (event.key === 'Enter' || event.key === ' ') {
                    event.preventDefault();
                    app.ui.openInfoDialog();
                }
            });
            app.ui.closeDialogOnBackdrop(elements.packDialog);
            app.ui.closeDialogOnBackdrop(elements.infoDialog);
            app.ui.closeDialogOnBackdrop(elements.helpDialog);
            elements.packDialog.addEventListener('close', app.ui.scheduleGameplayFocusRestore);
            elements.infoDialog.addEventListener('close', app.ui.scheduleGameplayFocusRestore);
            elements.helpDialog.addEventListener('close', app.ui.scheduleGameplayFocusRestore);
            elements.successDialog.addEventListener('close', app.ui.scheduleGameplayFocusRestore);
            elements.successDialog.addEventListener('click', app.ui.handleSuccessDialogInteraction);
            elements.successDialog.addEventListener('cancel', event => {
                event.preventDefault();
            });
        },

        initializeWindowHandlers() {
            window.addEventListener('keydown', app.ui.handleKeyPress);
            elements.board.addEventListener('click', app.board.handleBoardTileClick);
            elements.undoButton.addEventListener('click', app.ui.handleUndoButtonClick);
            elements.undoButton.addEventListener('pointerdown', app.ui.handleUndoButtonPointerDown);
            elements.undoButton.addEventListener('pointerup', app.ui.handleUndoButtonPointerEnd);
            elements.undoButton.addEventListener('pointercancel', app.ui.handleUndoButtonPointerEnd);
            window.addEventListener('focus', app.ui.scheduleGameplayFocusRestore);
            window.addEventListener('blur', app.ui.stopUndoRepeat);
            document.addEventListener('visibilitychange', () => {
                if (document.hidden) {
                    app.ui.stopUndoRepeat();
                    return;
                }
                app.ui.scheduleGameplayFocusRestore();
            });
            window.addEventListener('resize', () => {
                if (state.gameReady) {
                    window.requestAnimationFrame(() => {
                        app.board.updateCamera(true);
                    });
                }
            });
        },

        bootstrap() {
            Module.onRuntimeInitialized = async () => {
                app.core.bindRuntimeApi();
                app.ui.refreshBuiltInPackList();
                app.ui.initializePackHandlers();
                app.ui.initializeDialogHandlers();
                app.ui.initializeWindowHandlers();
                window.focusGameplaySurface = app.ui.focusGameplaySurface;
                await app.core.initGame();
            };
        },
    };

    app.ui.bootstrap();
})();
