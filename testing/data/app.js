"use strict";
const $ = id => document.getElementById(id);
const randomHex = bytes => Array.from(crypto.getRandomValues(new Uint8Array(bytes)), b => b.toString(16).padStart(2, "0")).join("");
// Captive browsers can deny persistent storage. Keep the page usable for this visit.
const transientStorage = new Map();
let storageUnavailable = false;
const browserStorage = {
    getItem(key) { try { return localStorage.getItem(key) ?? transientStorage.get(key) ?? null; } catch { storageUnavailable = true; return transientStorage.get(key) ?? null; } },
    setItem(key, value) { transientStorage.set(key, value); try { localStorage.setItem(key, value); } catch { storageUnavailable = true; } },
    removeItem(key) { transientStorage.delete(key); try { localStorage.removeItem(key); } catch { storageUnavailable = true; } }
};
let session = browserStorage.getItem("heltec-session");
if (!/^[0-9a-f]{32}$/.test(session || "")) { session = randomHex(16); browserStorage.setItem("heltec-session", session); }
let status = {}, records = [], selected = browserStorage.getItem("heltec-selected") || "", polling = false, pendingSelection = "";
let rescuerAuth = "";
let actionBusy = false, clearing = false, viewGeneration = 0;
const photoURLs = new Map();
const coordinateHint = "Automatic phone GPS usually needs HTTPS. Enter latitude and longitude manually, or send your written location.";
function enteredCoordinates() {
    const latText = $("latitude").value.trim(), lonText = $("longitude").value.trim();
    if (!latText && !lonText) return {};
    if (!latText || !lonText) throw new Error("Enter both latitude and longitude, or leave both empty.");
    const lat = Number(latText), lon = Number(lonText);
    if (!Number.isFinite(lat) || !Number.isFinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) throw new Error("Latitude must be between -90 and 90; longitude between -180 and 180.");
    return { lat, lon };
}
const flags = ["Trapped", "Injured", "Needs water", "Needs medicine", "Unconscious"];
const note = text => { $("feedback").textContent = text; };
const text = (tag, value, className = "") => { const el = document.createElement(tag); el.textContent = value; el.className = className; return el; };
const bytesOK = (value, max, name) => { if (new TextEncoder().encode(value).length > max) throw new Error(`${name} is too long (maximum ${max} UTF-8 bytes; some characters use several bytes).`); return value; };
const delivery = r => r.delivery === "pending" ? "Saved · awaiting device acknowledgement" : r.delivery === "relay" ? "Stored on relay · awaiting rescuer" : r.mine ? "Received by device" : "Received on this node";
async function api(path, body, binary = false) {
    const headers = { "X-Session": session };
    if (rescuerAuth) headers.Authorization = rescuerAuth;
    const options = { headers, credentials: "same-origin", cache: "no-store" };
    if (body !== undefined) { options.method = "POST"; headers["Content-Type"] = binary ? "image/jpeg" : "application/json"; options.body = binary ? body : JSON.stringify(body); }
    const response = await fetch(path, options);
    if (!response.ok) { let message = `Request failed (${response.status})`; try { message = (await response.json()).error || message; } catch {} const error = new Error(message); error.status = response.status; throw error; }
    return response;
}
async function jsonAPI(path, body) { return (await api(path, body)).json(); }
function saveIntent(key, create) {
    const saved = browserStorage.getItem(key);
    if (saved) return JSON.parse(saved);
    const value = create(); browserStorage.setItem(key, JSON.stringify(value)); return value;
}
function updateControls() {
    const busy = actionBusy || clearing;
    for (const id of ["sendSOS", "sendChat", "attachButton", "recover", "retry", "beacon", "resolve", "loginButton", "takePhoto", "takeAttachPhoto", "photo", "cameraPhoto", "attachPhoto", "attachCamera"]) $(id).disabled = busy;
    $("sendSOS").disabled = busy || !status.storage;
    $("clearAll").disabled = busy || !status.canClear || (status.role === "rescuer" && !rescuerAuth);
}
async function action(button, fn) {
    if (actionBusy || clearing) return;
    actionBusy = true; updateControls();
    try { await fn(); await refresh(); }
    catch (e) { note(`${e.message}\nIf the connection dropped, reconnect to this node and retry. Saved reports remain on the node.`); }
    finally { actionBusy = false; updateControls(); }
}
async function compressPhoto(file) {
    if (!file) throw new Error("Choose a photo first.");
    if (file.size > 15 * 1024 * 1024) throw new Error("Choose a photo smaller than 15 MB.");
    const url = URL.createObjectURL(file), image = new Image();
    try { image.src = url; await image.decode(); } finally { URL.revokeObjectURL(url); }
    for (const bound of [[160, 120], [120, 90], [80, 60]]) {
        const scale = Math.min(bound[0] / image.width, bound[1] / image.height, 1);
        const canvas = document.createElement("canvas"); canvas.width = Math.max(1, Math.round(image.width * scale)); canvas.height = Math.max(1, Math.round(image.height * scale));
        const ctx = canvas.getContext("2d", { willReadFrequently: true }); ctx.fillStyle = "white"; ctx.fillRect(0, 0, canvas.width, canvas.height); ctx.drawImage(image, 0, 0, canvas.width, canvas.height);
        const data = ctx.getImageData(0, 0, canvas.width, canvas.height);
        for (let i = 0; i < data.data.length; i += 4) { const gray = Math.round(.299 * data.data[i] + .587 * data.data[i + 1] + .114 * data.data[i + 2]); data.data[i] = data.data[i + 1] = data.data[i + 2] = gray; }
        ctx.putImageData(data, 0, 0);
        for (const quality of [.6, .45, .3, .2]) {
            const blob = await new Promise(resolve => canvas.toBlob(resolve, "image/jpeg", quality));
            if (blob && blob.size <= 8192) return blob;
        }
    }
    throw new Error("This photo could not be compressed below 8 KB. Choose another photo.");
}
async function attachPhoto(id, file) {
    note("The text report is saved. Compressing and uploading the photo to this node…");
    const blob = await compressPhoto(file);
    await api(`/api/image?id=${id}`, blob, true);
    note(`Photo saved (${blob.size} bytes). Its LoRa transfer starts after a rescuer acknowledges the SOS. Watch its progress below.`);
}
function connectPhotoPicker(cameraId, galleryId, buttonId, selectionId) {
    const camera = $(cameraId), gallery = $(galleryId), selection = $(selectionId);
    $(buttonId).addEventListener("click", () => { if (!actionBusy && !clearing) camera.click(); });
    camera.addEventListener("change", () => {
        if (!camera.files[0]) return;
        gallery.value = "";
        selection.textContent = `Photo ready: ${camera.files[0].name}`;
    });
    gallery.addEventListener("change", () => {
        if (!gallery.files[0]) return;
        camera.value = ""; selection.textContent = "";
    });
}
connectPhotoPicker("cameraPhoto", "photo", "takePhoto", "photoSelection");
connectPhotoPicker("attachCamera", "attachPhoto", "takeAttachPhoto", "attachSelection");
function render() {
    const rescuer = status.role === "rescuer";
    $("login").hidden = !rescuer || !!rescuerAuth;
    updateControls();
    $("civilian").hidden = rescuer; $("beacon").hidden = !rescuer; $("recover").hidden = rescuer || !status.recoverable;
    $("title").textContent = rescuer ? "Rescuer dashboard" : "Request help & stay in touch";
    $("node").textContent = `${rescuer ? "Rescuer" : "Civilian"} node ${status.node || ""} · Mode ${status.transferMode ?? "?"} (${status.transferName || "unknown"}) · Protocol ${status.protocol ?? "?"}`;
    $("connectionInfo").textContent = `Protocol ${status.protocol ?? "?"} · Open Wi-Fi · Local ${location.protocol === "https:" ? "HTTPS" : "HTTP"} · No internet required`;
    $("health").textContent = `${status.radio ? "Radio ready" : "Radio unavailable — messages stay queued"} · ${status.storage ? `Storage ${status.records}/${status.limit} records` : "STORAGE UNAVAILABLE — messages cannot be saved"} · Sent ${status.tx || 0} / received ${status.rx || 0} packets`;
    $("reportsHeading").textContent = rescuer ? "Incoming SOS reports" : "Your reports";
    const reports = records.filter(r => r.kind === 1).sort((a, b) => Number(a.resolved) - Number(b.resolved) || Number(Boolean(b.flags & 19)) - Number(Boolean(a.flags & 19)) || b.order - a.order);
    const selectionExists = reports.some(r => r.id === selected);
    // A poll can finish with older data while a newly submitted report is being saved.
    if (!selectionExists && (!pendingSelection || selected !== pendingSelection)) selected = reports[0]?.id || "";
    if (selectionExists) pendingSelection = "";
    $("empty").hidden = reports.length > 0; $("reports").replaceChildren();
    for (const r of reports) {
        const button = text("button", "", `report${r.id === selected ? " selected" : ""}`);
        button.append(text("strong", `${r.name || "Unnamed person"} · ${r.people} ${r.people === 1 ? "person" : "people"}`));
        button.append(text("div", r.location || "Location not specified"));
        button.append(text("span", r.resolved ? "Resolved" : delivery(r), `badge ${r.delivery}`));
        if (r.flags) button.append(text("div", flags.filter((_, i) => r.flags & (1 << i)).join(" · ")));
        button.addEventListener("click", () => { selected = r.id; browserStorage.setItem("heltec-selected", selected); render(); });
        $("reports").append(button);
    }
    const report = reports.find(r => r.id === selected); $("conversation").hidden = !report;
    if (!report) return;
    $("detail").replaceChildren(text("p", report.text || "No additional details.", "wrap"), text("p", `Report ${report.id}`, "small"));
    const hasCoordinates = typeof report.lat === "number" && Number.isFinite(report.lat) && typeof report.lon === "number" && Number.isFinite(report.lon);
    const coordinates = document.createElement("div"); coordinates.className = "grid coordinates";
    coordinates.append(text("p", `Latitude: ${hasCoordinates ? report.lat.toFixed(6) : "Not supplied"}`, "coordinate-value"), text("p", `Longitude: ${hasCoordinates ? report.lon.toFixed(6) : "Not supplied"}`, "coordinate-value"));
    $("detail").append(coordinates);
    const related = records.filter(r => r.link === selected).sort((a, b) => a.order - b.order);
    const image = related.find(r => r.kind === 3); $("photoView").replaceChildren();
    if (image) {
        const label = image.delivery === "received" && image.mine ? "Photo received by rescuer device" : !image.mine && image.complete ? "Photo complete and verified" : `Photo ${image.mine ? "delivery" : "reception"}: ${image.done}/${image.chunks} chunks acknowledged`;
        $("photoView").append(text("p", label, "small"));
        if (image.complete) {
            if (photoURLs.has(image.id)) { const img = document.createElement("img"); img.src = photoURLs.get(image.id); img.alt = "Photo attached to this SOS report"; $("photoView").append(img); }
            else loadPhoto(image.id);
        }
    }
    $("attachArea").hidden = rescuer || !!image || report.resolved;
    const messages = $("messages"), nearBottom = messages.scrollHeight - messages.scrollTop - messages.clientHeight < 60;
    messages.replaceChildren();
    for (const r of related.filter(r => r.kind === 2 || r.kind === 5)) {
        const card = text("div", r.kind === 5 ? "Rescuer marked this report resolved." : r.text, `message${r.mine ? " mine" : ""}`);
        card.append(text("small", `${r.mine ? "You" : rescuer ? "Civilian" : "Rescuer"} · ${delivery(r)}`)); messages.append(card);
    }
    if (!messages.children.length) messages.append(text("p", "No conversation yet. Messages can be saved while waiting for a rescuer connection.", "small"));
    if (nearBottom) messages.scrollTop = messages.scrollHeight;
    $("resolve").hidden = !rescuer || report.resolved; $("chatForm").hidden = report.resolved;
}
const photosLoading = new Set();
async function loadPhoto(id) {
    if (photosLoading.has(id)) return; photosLoading.add(id);
    const generation = viewGeneration;
    try { const blob = await (await api(`/api/image?id=${id}`)).blob(); if (generation !== viewGeneration) return; photoURLs.set(id, URL.createObjectURL(blob)); render(); }
    catch {} finally { photosLoading.delete(id); }
}
async function refresh() {
    if (polling || clearing) return; polling = true;
    const generation = viewGeneration;
    try {
        const nextStatus = await jsonAPI("/api/status");
        if (generation !== viewGeneration) return;
        status = nextStatus; updateControls();
        if (nextStatus.role === "rescuer" && !rescuerAuth) { status = nextStatus; records = []; render(); return; }
        const result = await jsonAPI("/api/state");
        if (generation !== viewGeneration) return;
        status = nextStatus; records = result.records; render();
    }
    catch (e) { if (generation === viewGeneration) $("health").textContent = `Connection unavailable: ${e.message}. Stay connected to this node's Wi-Fi.`; }
    finally { polling = false; }
}
function resetClearedView() {
    const reports = new Set(records.filter(r => r.kind === 1).map(r => r.id));
    if (selected) reports.add(selected);
    for (const id of reports) {
        browserStorage.removeItem(`heltec-chat-draft-${id}`);
        browserStorage.removeItem(`heltec-resolve-${id}`);
    }
    browserStorage.removeItem(`heltec-sos-draft-${status.node}`);
    browserStorage.removeItem("heltec-selected");
    for (const url of photoURLs.values()) URL.revokeObjectURL(url);
    photoURLs.clear(); photosLoading.clear();
    records = []; selected = ""; pendingSelection = "";
    status.records = 0; status.recoverable = 0;
    $("sosForm").reset(); $("chatForm").reset(); $("attachPhoto").value = "";
    $("attachCamera").value = "";
    $("photoSelection").textContent = ""; $("attachSelection").textContent = "";
    $("gpsStatus").textContent = coordinateHint;
    for (const id of ["detail", "messages", "photoView"]) $(id).replaceChildren();
    render();
}
$("clearAll").addEventListener("click", async () => {
    if (actionBusy || clearing || !status.canClear) return;
    if (!confirm(`Clear ALL data on this ${status.role} node (${status.node})?\n\nThis permanently deletes every SOS report, chat message and photo, including other users' records, and cancels pending deliveries. Other nodes keep their copies.\n\nThis cannot be undone.`)) return;
    clearing = true; viewGeneration++; updateControls();
    note("Clearing reports, messages and photos on this node…");
    try {
        const result = await jsonAPI("/api/clear", { confirm: "CLEAR_ALL", node: status.node });
        if (result.ok !== true) throw new Error("The node did not confirm completion.");
        resetClearedView();
        note(result.radio === false ? "All data cleared. Radio unavailable; restart this node before sending again." : "All reports, messages and photos cleared from this node. Clear the other node separately if needed.");
    } catch (e) {
        note(`Clear all was not confirmed: ${e.message}\nSome data may already be deleted. Reconnect to this node, check its records and retry Clear all if needed.`);
    } finally { clearing = false; updateControls(); await refresh(); }
});
$("gpsButton").addEventListener("click", () => {
    if (!window.isSecureContext || !navigator.geolocation) { $("gpsStatus").textContent = coordinateHint; return; }
    $("gpsButton").disabled = true; $("gpsStatus").textContent = "Getting your phone location…";
    navigator.geolocation.getCurrentPosition(position => {
        $("latitude").value = position.coords.latitude.toFixed(6); $("longitude").value = position.coords.longitude.toFixed(6);
        $("gpsStatus").textContent = "Phone coordinates filled in. Check them before sending."; $("gpsButton").disabled = false;
    }, () => { $("gpsStatus").textContent = "GPS could not be read. Your entered coordinates are unchanged; you can enter them manually."; $("gpsButton").disabled = false; }, { enableHighAccuracy: true, timeout: 12000, maximumAge: 30000 });
});
$("sosForm").addEventListener("submit", event => {
    event.preventDefault(); action($("sendSOS"), async () => {
        const key = `heltec-sos-draft-${status.node}`;
        const payload = saveIntent(key, () => ({ id: randomHex(8), timestamp: Math.floor(Date.now() / 1000), people: Number($("people").value), flags: Array.from(document.querySelectorAll("input[name=flag]:checked")).reduce((sum, input) => sum | Number(input.value), 0), name: bytesOK($("name").value.trim(), 31, "Name"), location: bytesOK($("location").value.trim(), 47, "Location"), text: bytesOK($("sosText").value.trim(), 95, "Message"), ...enteredCoordinates() }));
        let result; try { result = await jsonAPI("/api/sos", payload); } catch(e) { if (e.status >= 400 && e.status < 500) browserStorage.removeItem(key); throw e; } selected = result.id; pendingSelection = result.id; browserStorage.setItem("heltec-selected", selected); browserStorage.removeItem(key);
        note("SOS saved on this node. Waiting for a rescuer device to acknowledge it.");
        const file = $("cameraPhoto").files[0] || $("photo").files[0]; if (file) { try { await attachPhoto(selected, file); } catch(e) { note(`SOS saved. Photo was not attached: ${e.message} Select the report below to retry its photo.`); } }
        $("sosForm").reset(); $("photoSelection").textContent = ""; $("gpsStatus").textContent = coordinateHint;
    });
});
$("chatForm").addEventListener("submit", event => {
    event.preventDefault(); action($("sendChat"), async () => {
        const key = `heltec-chat-draft-${selected}`;
        const payload = saveIntent(key, () => ({ id: randomHex(8), report: selected, timestamp: Math.floor(Date.now() / 1000), text: bytesOK($("chatText").value.trim(), 159, "Chat message") }));
        if (!payload.text) { browserStorage.removeItem(key); throw new Error("Enter a message."); }
        try { await jsonAPI("/api/chat", payload); } catch(e) { if (e.status >= 400 && e.status < 500) browserStorage.removeItem(key); throw e; } browserStorage.removeItem(key); $("chatText").value = ""; note("Message saved. Waiting for the other node's acknowledgement.");
    });
});
$("attachButton").addEventListener("click", () => action($("attachButton"), async () => {
    await attachPhoto(selected, $("attachCamera").files[0] || $("attachPhoto").files[0]);
    $("attachPhoto").value = ""; $("attachCamera").value = ""; $("attachSelection").textContent = "";
}));
$("recover").addEventListener("click", () => action($("recover"), async () => { await jsonAPI("/api/recover", {}); note("Earlier queued SOS reports are now linked to this browser. Their delivery retries continue automatically."); }));
$("retry").addEventListener("click", () => action($("retry"), async () => { await jsonAPI("/api/retry", {}); note("Pending messages scheduled for another attempt."); }));
$("beacon").addEventListener("click", () => action($("beacon"), async () => { await jsonAPI("/api/beacon", {}); note("Rescuer beacon scheduled."); }));
$("resolve").addEventListener("click", () => {
    if (!confirm("Mark this incident resolved? Do this only after help has been provided.")) return;
    action($("resolve"), async () => {
        const key = `heltec-resolve-${selected}`, payload = saveIntent(key, () => ({ id: randomHex(8), report: selected, timestamp: Math.floor(Date.now() / 1000) }));
        await jsonAPI("/api/resolve", payload); browserStorage.removeItem(key); note("Marked resolved here. The update will be retried until the civilian node acknowledges it.");
    });
});
$("loginForm").addEventListener("submit", event => {
    event.preventDefault(); action($("loginButton"), async () => {
        rescuerAuth = "Basic " + btoa("rescuer:" + $("loginPassword").value);
        try { records = (await jsonAPI("/api/state")).records; $("loginPassword").value = ""; note("Rescuer login successful."); }
        catch(e) { rescuerAuth = ""; throw e; }
    });
});
if (storageUnavailable) {
    $("sessionNotice").hidden = false;
    $("sessionNotice").textContent = "This connection window cannot remember your reports after it closes. For ongoing chat, open http://192.168.4.1/ in your normal browser before sending your report.";
}
refresh(); setInterval(() => { if (!document.hidden) refresh(); }, 5000);
