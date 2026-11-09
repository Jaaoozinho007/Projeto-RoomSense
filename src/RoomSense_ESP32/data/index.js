
// RoomSense - Dashboard conectado ao ESP32
// O navegador atua como cliente; o ESP32 hospeda a página e a API.
"use strict";

const API = "";
const POLL_INTERVAL = 1000;

const state = {
    connected: false,
    occupancy: 0,
    capacity: 10,
    entries: 0,
    exits: 0,
    movement: "parado",
    ledRed: false,
    ledGreen: true,
    actuatorMode: "auto",
    logs: [],
    lastLogSignature: ""
};

const $ = (id) => document.getElementById(id);

// Elementos existentes no index.html
const elements = {
    // Conexão
    connectionPill: $("connection-pill"),
    connectionDot: $("connection-dot"),
    connectionText: $("connection-text"),
    deviceDot: $("device-state-dot"),
    deviceText: $("device-state-text"),
    sidebarDot: $("sidebar-status-dot"),
    sidebarStatus: $("sidebar-device-status"),
    updatedLabel: $("updated-label"),

    // Ocupação
    occupancy: $("occupancy-value"),
    capacityPercent: $("capacity-percent"),
    progress: $("occupancy-progress"),
    available: $("available-spaces"),
    capacityLabel: $("capacity-label"),

    // Ambiente
    roomState: $("room-state"),
    roomDot: $("room-state-dot"),
    roomDescription: $("room-description"),

    // Movimentação
    movement: $("movement-state"),
    movementDescription: $("movement-description"),
    entries: $("entries-total"),
    exits: $("exits-total"),
    flowArrow: $("flow-arrow"),
    flowResult: $("flow-result"),
    simulateEntry: $("simulate-entry"),
    simulateExit: $("simulate-exit"),

    // Sensores
    entrySensorIndicator: $("entry-sensor-indicator"),
    exitSensorIndicator: $("exit-sensor-indicator"),
    entrySensorStatus: $("entry-sensor-status"),
    exitSensorStatus: $("exit-sensor-status"),

    // Atuadores
    redPreview: $("red-led-preview"),
    greenPreview: $("green-led-preview"),
    redToggle: $("red-led-toggle"),
    greenToggle: $("green-led-toggle"),
    redState: $("red-led-state"),
    greenState: $("green-led-state"),
    actuatorHint: $("actuator-hint"),

    // Logs e mensagens
    logList: $("log-list"),
    logCount: $("log-count"),
    feedback: $("simulator-feedback")
};

function getTime() {
    return new Date().toLocaleTimeString("pt-BR", {
        hour12: false
    });
}

// Verifica se todos os elementos obrigatórios existem
function checkElements() {
    const missing = Object.entries(elements)
        .filter(([name, element]) => element === null)
        .map(([name]) => name);

    if (missing.length > 0) {
        console.error(
            "RoomSense: elementos HTML não encontrados:",
            missing
        );

        return false;
    }

    return true;
}

// Atualiza o indicador de conexão
function setConnection(connected) {
    const changed = state.connected !== connected;

    state.connected = connected;

    const statusClass = connected ? "online" : "offline";

    elements.connectionDot.className =
        `status-dot ${statusClass}`;

    elements.deviceDot.className =
        `status-dot ${statusClass}`;

    elements.sidebarDot.className =
        `status-dot ${statusClass}`;

    elements.connectionText.textContent =
        connected ? "ESP32 conectado" : "ESP32 desconectado";

    elements.deviceText.textContent =
        connected ? "Conectado" : "Desconectado";

    elements.sidebarStatus.textContent =
        connected ? "Conectado à rede" : "Aguardando conexão";

    elements.connectionPill.classList.toggle(
        "connected",
        connected
    );

    elements.updatedLabel.innerHTML =
        `<span class="pulse-dot"></span> ${
            connected
                ? "Atualização em tempo real"
                : "Sem atualização"
        }`;

    if (changed) {
        addLog(
            connected ? "success" : "warning",
            connected
                ? "Comunicação com o ESP32 estabelecida."
                : "Comunicação com o ESP32 perdida."
        );
    }
}

// Registra eventos locais
function addLog(level, message, time = getTime()) {
    const key = `${time}|${level}|${message}`;

    if (key === state.lastLogSignature) return;

    state.lastLogSignature = key;

    state.logs.unshift({
        time,
        level,
        message
    });

    if (state.logs.length > 100) {
        state.logs.pop();
    }

    renderLogs();
}

// Atualiza a lista de logs
function renderLogs() {
    elements.logList.innerHTML = "";

    if (!state.logs.length) {
        const empty = document.createElement("div");

        empty.className = "log-empty";

        empty.textContent =
            "Aguardando eventos do sistema...";

        elements.logList.appendChild(empty);

    } else {
        state.logs.forEach(entry => {
            const row = document.createElement("div");

            row.className = "log-entry";

            const time = document.createElement("span");

            time.className = "log-time";

            time.textContent = `[${entry.time}]`;

            const level = document.createElement("span");

            level.className =
                `log-level ${entry.level}`;

            level.textContent =
                entry.level.toUpperCase();

            const message = document.createElement("span");

            message.className = "log-message";

            message.textContent = entry.message;

            row.append(time, level, message);

            elements.logList.appendChild(row);
        });
    }

    elements.logCount.textContent =
        `${state.logs.length} ${
            state.logs.length === 1
                ? "evento"
                : "eventos"
        }`;
}

// Atualiza os dados de ocupação
function updateOccupancy() {
    const percent = state.capacity > 0
        ? Math.round(
            state.occupancy / state.capacity * 100
        )
        : 0;

    const available = Math.max(
        0,
        state.capacity - state.occupancy
    );

    elements.occupancy.textContent =
        state.occupancy;

    elements.capacityPercent.textContent =
        `${percent}%`;

    elements.progress.style.width =
        `${percent}%`;

    elements.available.textContent =
        `${available} ${
            available === 1
                ? "vaga disponível"
                : "vagas disponíveis"
        }`;

    elements.capacityLabel.textContent =
        state.capacity;

    if (state.occupancy === 0) {
        elements.roomState.textContent = "Vazio";

        elements.roomDot.className =
            "large-state-dot empty";

        elements.roomDescription.textContent =
            "Nenhuma pessoa detectada no ambiente.";

    } else if (
        state.occupancy >= state.capacity
    ) {
        elements.roomState.textContent = "Lotado";

        elements.roomDot.className =
            "large-state-dot full";

        elements.roomDescription.textContent =
            "A capacidade máxima do ambiente foi atingida.";

    } else {
        elements.roomState.textContent = "Ocupado";

        elements.roomDot.className =
            "large-state-dot occupied";

        elements.roomDescription.textContent =
            "Há pessoas presentes no ambiente.";
    }

    elements.entries.textContent =
        state.entries;

    elements.exits.textContent =
        state.exits;
}

// Atualiza a movimentação
function updateMovement(direction) {
    state.movement = direction;

    const labels = {
        entrando: [
            "Entrada detectada",
            "Sequência: sensor de entrada → sensor de saída."
        ],

        saindo: [
            "Saída detectada",
            "Sequência: sensor de saída → sensor de entrada."
        ],

        parado: [
            "Sem movimentação",
            "Aguardando acionamento dos sensores."
        ]
    };

    const current =
        labels[direction] || labels.parado;

    elements.movement.textContent =
        current[0];

    elements.movementDescription.textContent =
        current[1];

    elements.flowArrow.className =
        "flow-arrow";

    elements.flowResult.className =
        "flow-result";

    const resultText =
        elements.flowResult.querySelector("strong");

    if (
        direction === "entrando" ||
        direction === "saindo"
    ) {
        elements.flowArrow.classList.add(
            direction === "entrando"
                ? "entering"
                : "exiting"
        );

        elements.flowResult.classList.add(
            direction === "entrando"
                ? "entering"
                : "exiting"
        );

        resultText.textContent =
            direction === "entrando"
                ? "Pessoa entrando"
                : "Pessoa saindo";

    } else {
        resultText.textContent =
            "Sem movimentação";
    }
}

// Atualiza os indicadores dos sensores
function setSensorActivity(sensor, active) {
    const isEntry =
        sensor === "entrada";

    const indicator = isEntry
        ? elements.entrySensorIndicator
        : elements.exitSensorIndicator;

    const label = isEntry
        ? elements.entrySensorStatus
        : elements.exitSensorStatus;

    indicator.classList.toggle(
        "active",
        active
    );

    label.textContent =
        active ? "Detectando" : "Livre";
}

// Atualiza os LEDs
function renderActuators() {
    const leds = [
        [
            state.ledRed,
            elements.redPreview,
            elements.redToggle,
            elements.redState
        ],

        [
            state.ledGreen,
            elements.greenPreview,
            elements.greenToggle,
            elements.greenState
        ]
    ];

    for (
        const [on, preview, toggle, label]
        of leds
    ) {
        preview.classList.toggle(
            "is-on",
            on
        );

        toggle.classList.toggle(
            "is-on",
            on
        );

        toggle.setAttribute(
            "aria-checked",
            String(on)
        );

        label.textContent =
            on ? "Ligado" : "Desligado";

        label.classList.toggle(
            "on",
            on
        );
    }

    const manual =
        state.actuatorMode === "manual";

    elements.redToggle.disabled =
        !manual || !state.connected;

    elements.greenToggle.disabled =
        !manual || !state.connected;

    $("auto-mode").classList.toggle(
        "active",
        !manual
    );

    $("manual-mode").classList.toggle(
        "active",
        manual
    );

    elements.actuatorHint.textContent = manual
        ? "Modo manual: os interruptores enviam comandos aos LEDs físicos do ESP32."
        : "Modo automático: verde indica vagas disponíveis; vermelho indica lotação.";
}

// Comunicação com a API do ESP32
async function api(path, options = {}) {
    const response = await fetch(
        `${API}${path}`,
        {
            ...options,
            cache: "no-store"
        }
    );

    if (!response.ok) {
        let detail = "";

        try {
            detail = (
                await response.json()
            ).error || "";

        } catch (_) {}

        throw new Error(
            detail || `HTTP ${response.status}`
        );
    }

    const type =
        response.headers.get("content-type") || "";

    return type.includes("application/json")
        ? response.json()
        : {};
}

// Envia comandos no formato de formulário
function postForm(path, values) {
    return api(
        path,
        {
            method: "POST",

            headers: {
                "Content-Type":
                    "application/x-www-form-urlencoded"
            },

            body: new URLSearchParams(values)
        }
    );
}

// Recebe e aplica os dados do ESP32
function applyStatus(data) {
    state.occupancy =
        Number(data.occupancy ?? 0);

    state.capacity =
        Number(data.capacity ?? 10);

    state.entries =
        Number(data.entries ?? 0);

    state.exits =
        Number(data.exits ?? 0);

    state.ledRed =
        Boolean(data.ledRed);

    state.ledGreen =
        Boolean(data.ledGreen);

    state.actuatorMode =
        data.mode || "auto";

    updateOccupancy();

    updateMovement(
        data.movement || "parado"
    );

    setSensorActivity(
        "entrada",
        Boolean(data.entryDetected)
    );

    setSensorActivity(
        "saida",
        Boolean(data.exitDetected)
    );

    renderActuators();
}

// Atualiza os logs recebidos do ESP32
async function refreshLogs() {
    const data =
        await api("/api/logs");

    if (Array.isArray(data.logs)) {
        state.logs = data.logs
            .slice(-100)
            .reverse()
            .map(item => ({
                time: item.time,
                level: item.level,
                message: item.message
            }));

        state.lastLogSignature = "";

        renderLogs();
    }
}

// Consulta o servidor periodicamente
async function pollESP32() {
    try {
        const data =
            await api("/api/status");

        setConnection(true);

        applyStatus(data);

        try {
            await refreshLogs();

        } catch (error) {
            console.warn(
                "Falha ao atualizar logs:",
                error
            );
        }

    } catch (error) {
        setConnection(false);

        console.warn(
            "RoomSense API:",
            error
        );
    }
}

// Executa comandos no ESP32
async function command(action, successMessage) {
    try {
        const data = await action();

        if (
            data &&
            data.occupancy !== undefined
        ) {
            applyStatus(data);
        }

        elements.feedback.textContent =
            successMessage;

        await pollESP32();

    } catch (error) {
        elements.feedback.textContent =
            `Não foi possível executar o comando: ${error.message}`;

        addLog(
            "warning",
            `Falha no comando: ${error.message}`
        );
    }
}

// Relógio
function updateClock() {
    const now = new Date();

    $("current-time").textContent =
        now.toLocaleTimeString(
            "pt-BR",
            { hour12: false }
        );

    $("current-date").textContent =
        now.toLocaleDateString(
            "pt-BR",
            {
                weekday: "short",
                day: "2-digit",
                month: "short",
                year: "numeric"
            }
        );
}

// ====== INICIALIZAÇÃO ======

function initializeDashboard() {
    if (!checkElements()) {
        console.error(
            "RoomSense: inicialização interrompida."
        );

        return;
    }

    $("footer-year").textContent =
        new Date().getFullYear();

    // Botões de entrada e saída habilitados
    elements.simulateEntry.disabled = false;
    elements.simulateExit.disabled = false;

    elements.simulateEntry.title =
        "Registrar manualmente uma entrada pelo dashboard.";

    elements.simulateExit.title =
        "Registrar manualmente uma saída pelo dashboard.";

    elements.feedback.textContent =
        "Aguardando conexão com o ESP32.";

    $("reset-demo").textContent =
        "↺ Zerar contador físico";

    // ====== BOTÃO DE ENTRADA ======
    elements.simulateEntry.addEventListener(
        "click",
        () => {
            command(
                () => api(
                    "/api/entry",
                    { method: "POST" }
                ),

                "Entrada registrada pelo dashboard."
            );
        }
    );

    // ====== BOTÃO DE SAÍDA ======
    elements.simulateExit.addEventListener(
        "click",
        () => {
            command(
                () => api(
                    "/api/exit",
                    { method: "POST" }
                ),

                "Saída registrada pelo dashboard."
            );
        }
    );

    // ====== RESET DO CONTADOR ======
    $("reset-demo").addEventListener(
        "click",
        () => {
            command(
                () => api(
                    "/api/reset",
                    { method: "POST" }
                ),

                "Contador do ESP32 reiniciado."
            );
        }
    );

    // ====== LIMPAR LOGS ======
    $("clear-logs").addEventListener(
        "click",
        () => {
            command(
                () => api(
                    "/api/logs/clear",
                    { method: "POST" }
                ),

                "Histórico de logs limpo no ESP32."
            );
        }
    );

    // ====== LED VERMELHO ======
    elements.redToggle.addEventListener(
        "click",
        () => {
            if (
                state.actuatorMode !== "manual"
            ) {
                return;
            }

            command(
                () => postForm(
                    "/api/led",
                    {
                        color: "red",

                        on: state.ledRed
                            ? "0"
                            : "1"
                    }
                ),

                "Comando enviado ao LED vermelho."
            );
        }
    );

    // ====== LED VERDE ======
    elements.greenToggle.addEventListener(
        "click",
        () => {
            if (
                state.actuatorMode !== "manual"
            ) {
                return;
            }

            command(
                () => postForm(
                    "/api/led",
                    {
                        color: "green",

                        on: state.ledGreen
                            ? "0"
                            : "1"
                    }
                ),

                "Comando enviado ao LED verde."
            );
        }
    );

    // ====== MODO AUTOMÁTICO ======
    $("auto-mode").addEventListener(
        "click",
        () => {
            command(
                () => postForm(
                    "/api/mode",
                    { mode: "auto" }
                ),

                "Modo automático ativado no ESP32."
            );
        }
    );

    // ====== MODO MANUAL ======
    $("manual-mode").addEventListener(
        "click",
        () => {
            command(
                () => postForm(
                    "/api/mode",
                    { mode: "manual" }
                ),

                "Modo manual ativado no ESP32."
            );
        }
    );

    // ====== NAVEGAÇÃO ======
    document
        .querySelectorAll(".nav-item")
        .forEach(item => {
            item.addEventListener(
                "click",
                () => {
                    document
                        .querySelectorAll(".nav-item")
                        .forEach(nav => {
                            nav.classList.remove("active");
                        });

                    item.classList.add("active");
                }
            );
        });

    // ====== INICIALIZAÇÃO FINAL ======
    updateClock();

    window.setInterval(
        updateClock,
        1000
    );

    renderLogs();

    updateMovement("parado");

    renderActuators();

    addLog(
        "info",
        "Dashboard RoomSense iniciado; aguardando resposta do servidor ESP32."
    );

    pollESP32();

    window.setInterval(
        pollESP32,
        POLL_INTERVAL
    );
}

document.addEventListener(
    "DOMContentLoaded",
    initializeDashboard
);
