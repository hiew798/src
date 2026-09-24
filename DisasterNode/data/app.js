/**
 * =====================================================================================
 * File: app.js
 * Project: Heltec Wireless Stick Lite V3 - Disaster Recovery Multi-Node LoRa Network
 * Author: Antigravity AI / FYP Disaster Recovery Team
 * 
 * Description:
 *   Frontend client JavaScript for the Emergency Captive Portal web application.
 *   Runs in the mobile browser of civilians connected to the node's WiFi Access Point.
 *   
 *   Key Responsibilities:
 *   - Form submission via asynchronous AJAX fetch to /api/sos.
 *   - Local validation and interactive victim counter (+/- buttons).
 *   - Periodic background polling of node status (/api/status) and incident list (/api/distress-list).
 *   - Dynamic rendering of emergency status badges (PENDING RESCUE vs RESCUED / RESOLVED).
 * =====================================================================================
 */

document.addEventListener('DOMContentLoaded', () => {
    // Current victim counter state
    let victimCount = 1;

    // DOM Element References
    const countDisplay   = document.getElementById('victimCountValue');
    const btnMinus       = document.getElementById('btnMinusCount');
    const btnPlus        = document.getElementById('btnPlusCount');
    const sosForm        = document.getElementById('sosForm');
    const statusAlert    = document.getElementById('statusAlert');
    const nodeStatusText = document.getElementById('nodeStatusText');
    const distressCards  = document.getElementById('distressCards');
    const distressCount  = document.getElementById('distressCount');
    const btnRefresh     = document.getElementById('btnRefreshList');

    // =================================================================================
    // 1. VICTIM COUNTER CONTROLS
    // =================================================================================
    btnMinus.addEventListener('click', () => {
        if (victimCount > 1) {
            victimCount--;
            countDisplay.textContent = victimCount;
        }
    });

    btnPlus.addEventListener('click', () => {
        if (victimCount < 99) {
            victimCount++;
            countDisplay.textContent = victimCount;
        }
    });

    // =================================================================================
    // 2. SOS FORM SUBMISSION HANDLER
    // Sends distress data as JSON to the ESP32 Async Web Server at /api/sos.
    // The ESP32 converts this JSON into a compact binary LoRa packet and broadcasts it.
    // =================================================================================
    sosForm.addEventListener('submit', async (e) => {
        e.preventDefault(); // Prevent full page reload

        // Collect form data into JSON payload
        const payload = {
            trapped: document.getElementById('chkTrapped').checked,
            injured: document.getElementById('chkInjured').checked,
            needWater: document.getElementById('chkNeedWater').checked,
            needMeds: document.getElementById('chkNeedMeds').checked,
            victimCount: victimCount,
            location: document.getElementById('txtLocation').value.trim(),
            name: document.getElementById('txtName').value.trim() || 'Anonymous',
            text: document.getElementById('txtMessage').value.trim()
        };

        const submitBtn = document.getElementById('btnSubmitSos');
        submitBtn.disabled = true;
        submitBtn.innerHTML = '📡 TRANSMITTING VIA LORA...';

        try {
            const response = await fetch('/api/sos', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });

            const data = await response.json();

            if (data.success) {
                showAlert(`✅ SOS Alert Broadcast! Message ID: ${data.msgId}`, 'success');
                fetchDistressList(); // Immediately refresh the visible reports list
            } else {
                showAlert('❌ Failed to send SOS alert. Please try again.', 'error');
            }
        } catch (err) {
            console.error('Submit error:', err);
            showAlert('⚠️ Network error transmitting SOS. Your device may be reconnecting.', 'error');
        } finally {
            submitBtn.disabled = false;
            submitBtn.innerHTML = '<span class="sos-icon">📡</span> SEND DISTRESS ALERT VIA LORA';
        }
    });

    // =================================================================================
    // 3. NODE DIAGNOSTICS POLLING
    // Periodically checks if the phone is still connected to the ESP32 node and displays
    // the Node ID and count of offline pending alerts stored in LittleFS.
    // =================================================================================
    async function fetchNodeStatus() {
        try {
            const res = await fetch('/api/status');
            if (res.ok) {
                const data = await res.json();
                nodeStatusText.textContent = `Connected to Node: 0x${data.nodeId} | Stored Offline: ${data.offlineSosCount}`;
            }
        } catch (e) {
            nodeStatusText.textContent = 'Node Status: Reconnecting...';
        }
    }

    // =================================================================================
    // 4. INCIDENT LIST RENDERING & STATUS SYNCHRONIZATION
    // Fetches all distress alerts known to this node and renders them with color-coded
    // emergency tags and lifecycle status badges (Awaiting Rescue vs Rescued).
    // =================================================================================
    async function fetchDistressList() {
        try {
            const res = await fetch('/api/distress-list');
            if (res.ok) {
                const list = await res.json();
                distressCount.textContent = list.length;

                if (list.length === 0) {
                    distressCards.innerHTML = '<p class="empty-msg">No active distress reports found on this node.</p>';
                    return;
                }

                distressCards.innerHTML = list.map(item => {
                    // status: 0 = PENDING, 1 = ACKNOWLEDGED, 2 = RESOLVED
                    const isResolved = item.status === 2;
                    return `
                        <div class="distress-card ${isResolved ? 'status-resolved' : ''}">
                            <div class="card-header">
                                <span>📍 ${escapeHtml(item.location)}</span>
                                <span>👥 ${item.victimCount} People</span>
                            </div>
                            <div class="tag-list">
                                ${isResolved 
                                    ? '<span class="badge green">✓ RESCUED / RESOLVED</span>' 
                                    : '<span class="badge orange">AWAITING RESCUE</span>'}
                                ${item.trapped ? '<span class="badge red">TRAPPED</span>' : ''}
                                ${item.injured ? '<span class="badge red">INJURED</span>' : ''}
                                ${item.needWater ? '<span class="badge orange">NEED WATER</span>' : ''}
                                ${item.needMeds ? '<span class="badge orange">NEED MEDS</span>' : ''}
                            </div>
                            <p style="font-size: 0.8rem; color: #cbd5e1;">${escapeHtml(item.text || 'No additional message.')}</p>
                            <p style="font-size: 0.7rem; color: #64748b; margin-top: 4px;">By: ${escapeHtml(item.name)} | ID: ${item.msgId}</p>
                        </div>
                    `;
                }).join('');
            }
        } catch (e) {
            console.error('Failed to load distress list:', e);
        }
    }

    btnRefresh.addEventListener('click', fetchDistressList);

    /**
     * Displays a temporary banner alert at the top of the UI.
     */
    function showAlert(msg, type) {
        statusAlert.textContent = msg;
        statusAlert.className = `status-alert ${type}`;
        setTimeout(() => {
            statusAlert.className = 'status-alert hidden';
        }, 8000);
    }

    /**
     * Sanitizes user input strings to prevent Cross-Site Scripting (XSS).
     */
    function escapeHtml(str) {
        return str.replace(/[&<>"']/g, function(m) {
            return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#039;' }[m];
        });
    }

    // Initial load on page startup
    fetchNodeStatus();
    fetchDistressList();

    // Periodic polling timers
    setInterval(fetchNodeStatus, 5000);   // Check node connectivity every 5 seconds
    setInterval(fetchDistressList, 8000); // Check for new/resolved incident updates every 8 seconds
});
