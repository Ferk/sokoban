(() => {
    const app = window.SokobanWebApp;
    const { constants, elements, state } = app;

    function clamp(value, min, max) {
        return Math.min(Math.max(value, min), max);
    }

    app.board = {
        applyTileAppearance(tileElement, tileChar) {
            let hasPlayer = false;

            switch (tileChar) {
                case '#':
                    tileElement.classList.add('base-wall');
                    break;
                case '@':
                    tileElement.classList.add('overlay-player');
                    hasPlayer = true;
                    break;
                case '+':
                    tileElement.classList.add('base-goal', 'overlay-player');
                    hasPlayer = true;
                    break;
                case '&':
                    tileElement.classList.add('base-ice', 'overlay-player');
                    hasPlayer = true;
                    break;
                case '$':
                    tileElement.classList.add('overlay-box');
                    break;
                case '*':
                    tileElement.classList.add('base-goal', 'overlay-box');
                    break;
                case '"':
                    tileElement.classList.add('base-ice', 'overlay-box');
                    break;
                case '.':
                    tileElement.classList.add('base-goal');
                    break;
                case '~':
                    tileElement.classList.add('base-ice');
                    break;
            }

            return hasPlayer;
        },

        buildInfoBoardPreview() {
            const rows = app.runtime.getInitialRows();
            const cols = app.runtime.getInitialCols();
            const previewBoard = document.createElement('div');
            const fragment = document.createDocumentFragment();

            previewBoard.className = 'info-board-preview';
            previewBoard.style.setProperty('--cols', cols);

            for (let row = 0; row < rows; row++) {
                for (let col = 0; col < cols; col++) {
                    const tileChar = String.fromCharCode(app.runtime.getInitialTile(row, col));
                    const tileDiv = document.createElement('div');

                    tileDiv.classList.add('tile');
                    app.board.applyTileAppearance(tileDiv, tileChar);
                    fragment.appendChild(tileDiv);
                }
            }

            previewBoard.appendChild(fragment);
            return previewBoard;
        },

        updateCamera(forceCenter = false) {
            if (state.currentPlayerTile === null || elements.board.firstElementChild === null) {
                return;
            }

            const viewportRect = elements.boardViewport.getBoundingClientRect();
            const playerRect = state.currentPlayerTile.getBoundingClientRect();
            const viewportWidth = elements.boardViewport.clientWidth;
            const viewportHeight = elements.boardViewport.clientHeight;
            const currentScrollLeft = elements.boardViewport.scrollLeft;
            const currentScrollTop = elements.boardViewport.scrollTop;
            const maxScrollLeft = Math.max(0, elements.boardViewport.scrollWidth - viewportWidth);
            const maxScrollTop = Math.max(0, elements.boardViewport.scrollHeight - viewportHeight);
            const playerVisibleX = playerRect.left - viewportRect.left + playerRect.width / 2;
            const playerVisibleY = playerRect.top - viewportRect.top + playerRect.height / 2;
            const playerContentX = currentScrollLeft + playerVisibleX;
            const playerContentY = currentScrollTop + playerVisibleY;
            const marginX = viewportWidth * constants.cameraEdgeMarginRatio;
            const marginY = viewportHeight * constants.cameraEdgeMarginRatio;
            const safeLeft = currentScrollLeft + marginX;
            const safeRight = currentScrollLeft + viewportWidth - marginX;
            const safeTop = currentScrollTop + marginY;
            const safeBottom = currentScrollTop + viewportHeight - marginY;
            let nextScrollLeft = currentScrollLeft;
            let nextScrollTop = currentScrollTop;

            if (forceCenter) {
                nextScrollLeft = playerContentX - viewportWidth / 2;
                nextScrollTop = playerContentY - viewportHeight / 2;
            } else {
                if (playerContentX < safeLeft) {
                    nextScrollLeft = playerContentX - marginX;
                } else if (playerContentX > safeRight) {
                    nextScrollLeft = playerContentX - (viewportWidth - marginX);
                }

                if (playerContentY < safeTop) {
                    nextScrollTop = playerContentY - marginY;
                } else if (playerContentY > safeBottom) {
                    nextScrollTop = playerContentY - (viewportHeight - marginY);
                }
            }

            nextScrollLeft = clamp(nextScrollLeft, 0, maxScrollLeft);
            nextScrollTop = clamp(nextScrollTop, 0, maxScrollTop);

            if (Math.abs(nextScrollLeft - currentScrollLeft) > 0.5) {
                elements.boardViewport.scrollLeft = nextScrollLeft;
            }
            if (Math.abs(nextScrollTop - currentScrollTop) > 0.5) {
                elements.boardViewport.scrollTop = nextScrollTop;
            }
        },

        drawBoard(forceCenter = false) {
            const rows = app.runtime.getRows();
            const cols = app.runtime.getCols();
            const fragment = document.createDocumentFragment();

            state.currentPlayerTile = null;
            elements.board.innerHTML = '';
            elements.board.style.setProperty('--cols', cols);

            for (let row = 0; row < rows; row++) {
                for (let col = 0; col < cols; col++) {
                    const tileChar = String.fromCharCode(app.runtime.getTile(row, col));
                    const tileDiv = document.createElement('div');

                    tileDiv.classList.add('tile');
                    tileDiv.dataset.row = String(row);
                    tileDiv.dataset.col = String(col);
                    if (app.board.applyTileAppearance(tileDiv, tileChar)) {
                        state.currentPlayerTile = tileDiv;
                    }

                    fragment.appendChild(tileDiv);
                }
            }

            elements.board.appendChild(fragment);
            app.ui.refreshHeader();
            app.ui.refreshStatus();
            window.requestAnimationFrame(() => app.board.updateCamera(forceCenter));
        },

        handleBoardTileClick(event) {
            const tileElement = event.target.closest('.tile');

            if (tileElement === null || !elements.board.contains(tileElement)) {
                return;
            }
            if (!state.gameReady || state.loadingLevel || app.ui.isModalDialogOpen() || app.runtime.isEventOngoing() || app.runtime.isGameWon()) {
                return;
            }

            const row = Number(tileElement.dataset.row);
            const col = Number(tileElement.dataset.col);

            if (!Number.isInteger(row) || !Number.isInteger(col)) {
                return;
            }

            const moveSequence = app.runtime.planTapPath(row, col);
            if (moveSequence === '') {
                return;
            }

            event.preventDefault();
            app.ui.focusGameplaySurface();
            app.ui.queueAutoMoves(moveSequence);
        },
    };
})();
