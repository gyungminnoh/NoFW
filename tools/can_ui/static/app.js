const stateEls = {
  sessionInfo: document.getElementById("sessionInfo"),
  linkState: document.getElementById("linkState"),
  lastError: document.getElementById("lastError"),
  streamInfo: document.getElementById("streamInfo"),
  angleStatus: document.getElementById("angleStatus"),
  velocityStatus: document.getElementById("velocityStatus"),
  profileFeedback: document.getElementById("profileFeedback"),
  storedProfile: document.getElementById("storedProfile"),
  activeProfile: document.getElementById("activeProfile"),
  defaultMode: document.getElementById("defaultMode"),
  angleEnabled: document.getElementById("angleEnabled"),
  velocityEnabled: document.getElementById("velocityEnabled"),
  needCalibration: document.getElementById("needCalibration"),
  armedState: document.getElementById("armedState"),
  diagRaw: document.getElementById("diagRaw"),
  eventLog: document.getElementById("eventLog"),
};
const controls = {
  applySessionBtn: document.getElementById("applySessionBtn"),
  armBtn: document.getElementById("armBtn"),
  disarmBtn: document.getElementById("disarmBtn"),
  sendAngleBtn: document.getElementById("sendAngleBtn"),
  holdCurrentBtn: document.getElementById("holdCurrentBtn"),
  sendVelocityBtn: document.getElementById("sendVelocityBtn"),
  zeroVelocityBtn: document.getElementById("zeroVelocityBtn"),
  stopStreamBtn: document.getElementById("stopStreamBtn"),
  sendRawBtn: document.getElementById("sendRawBtn"),
  profileButtons: Array.from(document.querySelectorAll(".profile-btn")),
  anglePresetButtons: Array.from(document.querySelectorAll(".angle-preset")),
  velocityPresetButtons: Array.from(document.querySelectorAll(".velocity-preset")),
};
let lastState = null;
let pendingProfileRequest = null;
let profileFeedback = {
  text: "No profile request yet.",
  kind: "muted",
};

function fmtNumber(value, suffix) {
  if (value === null || value === undefined) return "-";
  return `${value.toFixed(3)} ${suffix}`;
}

function fmtBool(value) {
  if (value === null || value === undefined) return "-";
  return value ? "true" : "false";
}

function setButtonEnabled(button, enabled, reason = "") {
  button.disabled = !enabled;
  button.title = enabled ? "" : reason;
}

function setActive(button, active) {
  button.classList.toggle("active", !!active);
  button.setAttribute("aria-pressed", active ? "true" : "false");
}

function setPending(button, pending) {
  button.classList.toggle("pending", !!pending);
  button.setAttribute("aria-busy", pending ? "true" : "false");
}

function syncPresetState(inputEl, buttons) {
  const value = String(inputEl.value);
  for (const btn of buttons) {
    setActive(btn, btn.dataset.value === value);
  }
}

function updateProfileFeedback(diag, linkAlive) {
  if (!pendingProfileRequest) {
    stateEls.profileFeedback.textContent = profileFeedback.text;
    stateEls.profileFeedback.className = `feedback ${profileFeedback.kind}`;
    return;
  }

  const target = pendingProfileRequest.profile;
  const elapsedMs = performance.now() - pendingProfileRequest.requestedAt;
  const storedProfile = diag.stored_profile || null;
  const activeProfile = diag.active_profile || null;

  if (storedProfile === target && activeProfile === target) {
    profileFeedback = {
      text: `Profile applied: ${target}`,
      kind: "good",
    };
    pendingProfileRequest = null;
  } else if (!linkAlive) {
    profileFeedback = {
      text: `Requested ${target}; waiting for live 0x5F7 status.`,
      kind: "pending",
    };
  } else if (elapsedMs > 2500) {
    profileFeedback = {
      text: `Profile not applied: requested ${target}, current active=${activeProfile || "-"}.`,
      kind: "bad",
    };
    pendingProfileRequest = null;
  } else {
    profileFeedback = {
      text: `Requested ${target}; waiting for stored/active profile to match.`,
      kind: "pending",
    };
  }

  stateEls.profileFeedback.textContent = profileFeedback.text;
  stateEls.profileFeedback.className = `feedback ${profileFeedback.kind}`;
}

async function postJson(path, payload) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload || {}),
  });
  const data = await res.json();
  if (!res.ok || !data.ok) {
    throw new Error(data.error || `Request failed: ${res.status}`);
  }
  renderState(data.state);
}

async function runAction(fn) {
  try {
    await fn();
  } catch (err) {
    stateEls.lastError.textContent = err.message;
    if (lastState) {
      renderState(lastState);
    }
  }
}

function renderLog(logs) {
  stateEls.eventLog.innerHTML = "";
  for (const item of logs || []) {
    const line = document.createElement("div");
    line.className = `log-line ${item.level}`;
    line.textContent = `[${item.ts}] ${item.level.toUpperCase()}: ${item.message}`;
    stateEls.eventLog.appendChild(line);
  }
}

function renderState(state) {
  lastState = state;
  const diag = state.diag || {};
  const linkAlive = !!state.link_alive;
  const armed = !!diag.armed;
  const angleEnabled = !!diag.enable_output_angle_mode;
  const velocityEnabled = !!diag.enable_velocity_mode;
  const activeProfile = diag.active_profile || null;
  const streamEnabled = !!state.stream.enabled;
  const streamMode = state.stream.mode || null;

  stateEls.sessionInfo.textContent =
    `iface=${state.session.can_iface}, node_id=${state.session.node_id}`;
  stateEls.linkState.textContent = state.link_alive ? "LINK ALIVE" : "NO LIVE FRAMES";
  stateEls.linkState.className = `status-pill ${state.link_alive ? "good" : "bad"}`;
  stateEls.lastError.textContent = state.last_error || "";
  stateEls.streamInfo.textContent = state.stream.enabled
    ? `${state.stream.mode} = ${state.stream.value.toFixed(3)}`
    : "stream disabled";
  stateEls.angleStatus.textContent = fmtNumber(state.angle_deg, "deg");
  stateEls.velocityStatus.textContent = fmtNumber(state.velocity_deg_s, "deg/s");
  updateProfileFeedback(diag, linkAlive);
  stateEls.storedProfile.textContent = diag.stored_profile || "-";
  stateEls.activeProfile.textContent = diag.active_profile || "-";
  stateEls.defaultMode.textContent = diag.default_control_mode || "-";
  stateEls.angleEnabled.textContent = fmtBool(diag.enable_output_angle_mode);
  stateEls.velocityEnabled.textContent = fmtBool(diag.enable_velocity_mode);
  stateEls.needCalibration.textContent = fmtBool(diag.need_calibration);
  stateEls.armedState.textContent = fmtBool(diag.armed);
  stateEls.diagRaw.textContent = diag.raw_hex || "-";
  renderLog(state.logs);

  setButtonEnabled(controls.applySessionBtn, true);
  setButtonEnabled(
    controls.armBtn,
    linkAlive && !armed,
    !linkAlive ? "No live CAN frames" : "Already armed"
  );
  setButtonEnabled(
    controls.disarmBtn,
    linkAlive && armed,
    !linkAlive ? "No live CAN frames" : "Already disarmed"
  );
  setButtonEnabled(
    controls.sendAngleBtn,
    linkAlive && angleEnabled,
    !linkAlive ? "No live CAN frames" : "Angle mode is disabled by current profile"
  );
  setButtonEnabled(
    controls.holdCurrentBtn,
    linkAlive && angleEnabled && state.angle_deg !== null && state.angle_deg !== undefined,
    !linkAlive
      ? "No live CAN frames"
      : !angleEnabled
        ? "Angle mode is disabled by current profile"
        : "Current angle is not available yet"
  );
  setButtonEnabled(
    controls.sendVelocityBtn,
    linkAlive && velocityEnabled,
    !linkAlive ? "No live CAN frames" : "Velocity mode is disabled by current profile"
  );
  setButtonEnabled(
    controls.zeroVelocityBtn,
    linkAlive && velocityEnabled,
    !linkAlive ? "No live CAN frames" : "Velocity mode is disabled by current profile"
  );
  setButtonEnabled(
    controls.stopStreamBtn,
    streamEnabled,
    "No active latched command stream"
  );
  setButtonEnabled(
    controls.sendRawBtn,
    linkAlive,
    "No live CAN frames"
  );

  setActive(controls.armBtn, armed);
  setActive(controls.disarmBtn, !armed);
  setActive(controls.sendAngleBtn, streamEnabled && streamMode === "angle");
  setActive(controls.sendVelocityBtn, streamEnabled && streamMode === "velocity");
  setActive(controls.stopStreamBtn, !streamEnabled);

  for (const btn of controls.profileButtons) {
    const isCurrent = btn.dataset.profile === activeProfile;
    const isPending =
      pendingProfileRequest && pendingProfileRequest.profile === btn.dataset.profile;
    setActive(btn, isCurrent);
    setPending(btn, isPending);
    setButtonEnabled(
      btn,
      linkAlive && !isCurrent && !isPending,
      !linkAlive
        ? "No live CAN frames"
        : isPending
          ? "Waiting for 0x5F7 to confirm this profile"
          : "Already the active profile"
    );
  }

  const angleInput = document.getElementById("angleInput");
  const velocityInput = document.getElementById("velocityInput");
  syncPresetState(angleInput, controls.anglePresetButtons);
  syncPresetState(velocityInput, controls.velocityPresetButtons);
  for (const btn of controls.anglePresetButtons) {
    setButtonEnabled(
      btn,
      linkAlive && angleEnabled,
      !linkAlive ? "No live CAN frames" : "Angle mode is disabled by current profile"
    );
  }
  for (const btn of controls.velocityPresetButtons) {
    setButtonEnabled(
      btn,
      linkAlive && velocityEnabled,
      !linkAlive ? "No live CAN frames" : "Velocity mode is disabled by current profile"
    );
  }
}

async function refreshState() {
  try {
    const res = await fetch("/api/state");
    const data = await res.json();
    renderState(data);
  } catch (err) {
    stateEls.lastError.textContent = err.message;
  }
}

document.getElementById("applySessionBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/session", {
      can_iface: document.getElementById("canIface").value,
      node_id: Number(document.getElementById("nodeId").value),
    });
  })
);

for (const btn of document.querySelectorAll(".profile-btn")) {
  btn.addEventListener("click", async () =>
    runAction(async () => {
      pendingProfileRequest = {
        profile: btn.dataset.profile,
        requestedAt: performance.now(),
      };
      profileFeedback = {
        text: `Requested ${btn.dataset.profile}; waiting for stored/active profile to match.`,
        kind: "pending",
      };
      if (lastState) {
        renderState(lastState);
      }
      await postJson("/api/profile", { profile: btn.dataset.profile });
    })
  );
}

document.getElementById("armBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/power", { armed: true });
  })
);

document.getElementById("disarmBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/power", { armed: false });
  })
);

document.getElementById("sendAngleBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/angle", {
      deg: Number(document.getElementById("angleInput").value),
    });
  })
);

document.getElementById("holdCurrentBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/hold", {});
  })
);

document.getElementById("sendVelocityBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/velocity", {
      deg_s: Number(document.getElementById("velocityInput").value),
    });
  })
);

document.getElementById("zeroVelocityBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/zero_velocity", {});
  })
);

document.getElementById("stopStreamBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/stop_stream", {});
  })
);

document.getElementById("sendRawBtn").addEventListener("click", async () =>
  runAction(async () => {
    await postJson("/api/raw_send", {
      can_id: document.getElementById("rawCanId").value,
      payload: document.getElementById("rawPayload").value,
    });
  })
);

for (const btn of document.querySelectorAll(".angle-preset")) {
  btn.addEventListener("click", () => {
    document.getElementById("angleInput").value = btn.dataset.value;
    syncPresetState(document.getElementById("angleInput"), controls.anglePresetButtons);
  });
}

for (const btn of document.querySelectorAll(".velocity-preset")) {
  btn.addEventListener("click", () => {
    document.getElementById("velocityInput").value = btn.dataset.value;
    syncPresetState(
      document.getElementById("velocityInput"),
      controls.velocityPresetButtons
    );
  });
}

document.getElementById("angleInput").addEventListener("input", () => {
  syncPresetState(document.getElementById("angleInput"), controls.anglePresetButtons);
});

document.getElementById("velocityInput").addEventListener("input", () => {
  syncPresetState(
    document.getElementById("velocityInput"),
    controls.velocityPresetButtons
  );
});

refreshState();
setInterval(refreshState, 250);
