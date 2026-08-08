const FB_URL = "https://tebco-9e7a6-default-rtdb.asia-southeast1.firebasedatabase.app";
const FB_AUTH = "FKic265uaJqjTc19MjrL430nU6O9vsIZt4XIEr62";

// Demo credentials (replace with real auth backend in production)
const VALID_USERNAME = "admin";
const VALID_PASSWORD = "tebco2026";

// State
let patients = {};
let devices = {};
let schedules = {};
let batteryChart = null;
let batteryChartFull = null;
let pollTimer = null;

// ================= LOGIN =================
const elLoginScreen = document.getElementById('login-screen');
const elAppShell = document.getElementById('app-shell');
const elLoginForm = document.getElementById('login-form');
const elLoginError = document.getElementById('login-error');

function isLoggedIn() {
  return sessionStorage.getItem('tebco_logged_in') === 'true';
}

function showApp() {
  elLoginScreen.classList.add('hidden');
  elAppShell.classList.remove('hidden');
  const savedUser = sessionStorage.getItem('tebco_user') || 'Admin';
  document.getElementById('user-name-label').textContent = savedUser;
  if (!pollTimer) init();
}

function showLogin() {
  elAppShell.classList.add('hidden');
  elLoginScreen.classList.remove('hidden');
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
}

elLoginForm.addEventListener('submit', (e) => {
  e.preventDefault();
  const u = document.getElementById('login-username').value.trim();
  const p = document.getElementById('login-password').value;

  if (u === VALID_USERNAME && p === VALID_PASSWORD) {
    sessionStorage.setItem('tebco_logged_in', 'true');
    sessionStorage.setItem('tebco_user', u.charAt(0).toUpperCase() + u.slice(1));
    elLoginError.classList.add('hidden');
    elLoginForm.reset();
    showApp();
  } else {
    elLoginError.classList.remove('hidden');
  }
});

document.getElementById('logout-btn').addEventListener('click', () => {
  sessionStorage.removeItem('tebco_logged_in');
  sessionStorage.removeItem('tebco_user');
  showLogin();
});

// ================= NAVIGATION =================
const sectionMeta = {
  dashboard: { title: 'Hospital Command Center', subtitle: 'Real-time TEBCO Smart Dispenser Monitoring' },
  devices: { title: 'Active Devices', subtitle: 'Status & baterai seluruh perangkat TEBCO' },
  patients: { title: 'Patient Registry', subtitle: 'Data pasien terdaftar dalam sistem' },
  schedules: { title: 'Medication Schedules', subtitle: 'Kelola jadwal minum obat pasien' },
};

document.querySelectorAll('.nav-item').forEach(item => {
  item.addEventListener('click', () => {
    const section = item.dataset.section;
    document.querySelectorAll('.nav-item').forEach(i => i.classList.remove('active'));
    item.classList.add('active');

    document.querySelectorAll('.view').forEach(v => v.classList.add('hidden'));
    document.getElementById(`view-${section}`).classList.remove('hidden');

    document.getElementById('page-title').textContent = sectionMeta[section].title;
    document.getElementById('page-subtitle').textContent = sectionMeta[section].subtitle;
  });
});

// ================= DOM Elements =================
const elDevicesList = document.getElementById('devices-list');
const elDevicesListFull = document.getElementById('devices-list-full');
const elPatientsList = document.getElementById('patients-list');
const elPatientsListFull = document.getElementById('patients-list-full');
const elSchedulesList = document.getElementById('schedules-list');
const elSchedulesListFull = document.getElementById('schedules-list-full');
const elFormPatientId = document.getElementById('form-patient-id');

// ================= API Helpers =================
async function fbGet(path) {
  try {
    const res = await fetch(`${FB_URL}${path}.json?auth=${FB_AUTH}`);
    return await res.json();
  } catch (e) {
    console.error("Firebase GET error:", e);
    return null;
  }
}

async function fbPut(path, data) {
  try {
    const res = await fetch(`${FB_URL}${path}.json?auth=${FB_AUTH}`, {
      method: 'PUT',
      body: JSON.stringify(data)
    });
    return await res.json();
  } catch (e) {
    console.error("Firebase PUT error:", e);
    return null;
  }
}

async function fbDelete(path) {
  try {
    const res = await fetch(`${FB_URL}${path}.json?auth=${FB_AUTH}`, {
      method: 'DELETE'
    });
    return await res.json();
  } catch (e) {
    console.error("Firebase DELETE error:", e);
    return null;
  }
}

// ================= Data Fetchers =================
async function fetchDevices() {
  devices = await fbGet('/devices') || {};
  renderDevices();
  renderBatteryChart();
}

async function fetchPatients() {
  patients = await fbGet('/patients') || {};
  renderPatients();
  updatePatientDropdown();
}

async function fetchSchedules() {
  schedules = await fbGet('/schedules') || {};
  renderSchedules();
}

// ================= Helpers =================
function batteryColor(pct) {
  if (pct >= 60) return '#0d9488';
  if (pct >= 30) return '#f59e0b';
  return '#ef4444';
}

function deviceCardHtml(id, dev) {
  const status = dev.status || 'idle';
  const battery = dev.battery ?? 0;
  const assignedId = dev.assigned_patient || '';
  const assignedPatient = assignedId ? patients[assignedId] : null;
  const patientLabel = assignedPatient
    ? `${assignedId} &middot; ${assignedPatient.name}`
    : (assignedId ? assignedId : 'Belum dipasangkan');

  return `
    <div class="card device-card">
      <div class="gauge" style="--pct:${battery}; --gauge-color:${batteryColor(battery)}">
        <span>${battery}%</span>
      </div>
      <div class="device-body">
        <div class="card-header">
          <span class="card-title">${dev.alias || 'Unknown Room'}</span>
          <span class="badge ${status}">${status}</span>
        </div>
        <div class="card-details">
          <span>ID: ${id}</span>
          <span>Patient: <b>${patientLabel}</b></span>
          <span>IP: ${dev.ip || '-'}</span>
        </div>
      </div>
      <button class="btn-icon assign-btn" onclick="openAssignModal('${id}')" title="Pasangkan Pasien">
        <i class="ph ph-link-simple"></i>
      </button>
    </div>
  `;
}

function patientCardHtml(id, p) {
  return `
    <div class="card">
      <div class="card-header">
        <span class="card-title">${p.name || 'Unknown'}</span>
        <span class="badge pending">${id}</span>
      </div>
        <div class="card-details">
        <span>Age: ${p.age || '-'} | Gender: ${p.gender || '-'}</span>
        <span>Disease: ${p.disease || '-'}</span>
        <span>WA: ${p.wa_number || '-'}</span>
        <span style="color:#ef4444; font-weight:500;">Stok A: ${p.stock_servo1 || 0} | Stok B: ${p.stock_servo2 || 0} | Min: ${p.stock_threshold || 5}</span>
      </div>
      <div class="card-actions">
        <button class="btn-icon" onclick="editPatient('${id}')" title="Edit Pasien">
          <i class="ph ph-pencil-simple"></i>
        </button>
        <button class="btn-icon delete" onclick="deletePatient('${id}')" title="Hapus Pasien">
          <i class="ph ph-trash"></i>
        </button>
      </div>
    </div>
  `;
}

// ================= Renderers =================
function renderDevices() {
  const empty = '<div class="loading">No devices found.</div>';
  if (Object.keys(devices).length === 0) {
    elDevicesList.innerHTML = empty;
    elDevicesListFull.innerHTML = empty;
    return;
  }
  let html = '';
  for (const [id, dev] of Object.entries(devices)) {
    html += deviceCardHtml(id, dev);
  }
  elDevicesList.innerHTML = html;
  elDevicesListFull.innerHTML = html;
}

function renderPatients() {
  const empty = '<div class="loading">No patients found.</div>';
  if (Object.keys(patients).length === 0) {
    elPatientsList.innerHTML = empty;
    elPatientsListFull.innerHTML = empty;
    return;
  }
  let html = '';
  for (const [id, p] of Object.entries(patients)) {
    html += patientCardHtml(id, p);
  }
  elPatientsList.innerHTML = html;
  elPatientsListFull.innerHTML = html;
}

function renderSchedules() {
  const empty = '<tr><td colspan="6" class="text-center loading">No schedules found.</td></tr>';
  if (Object.keys(schedules).length === 0) {
    elSchedulesList.innerHTML = empty;
    elSchedulesListFull.innerHTML = empty;
    return;
  }

  let html = '';
  for (const [patientId, patientScheds] of Object.entries(schedules)) {
    if (!patientScheds) continue;
    for (const [schedId, s] of Object.entries(patientScheds)) {
      let statusClass = s.status ? s.status.toLowerCase() : 'pending';
      html += `
        <tr>
          <td><strong>${patientId}</strong><br><small style="color:#94a3b8">${patients[patientId]?.name || ''}</small></td>
          <td><strong style="font-size:1.05rem">${s.time || '--:--'}</strong></td>
          <td>${s.qty_servo1 || 0}</td>
          <td>${s.qty_servo2 || 0}</td>
          <td><span class="badge ${statusClass}">${s.status || 'Pending'}</span></td>
          <td>
            <button class="btn-icon" onclick="editSchedule('${patientId}', '${schedId}')" title="Edit">
              <i class="ph ph-pencil-simple"></i>
            </button>
            <button class="btn-icon delete" onclick="deleteSchedule('${patientId}', '${schedId}')" title="Delete">
              <i class="ph ph-trash"></i>
            </button>
          </td>
        </tr>
      `;
    }
  }
  const finalHtml = html || empty;
  elSchedulesList.innerHTML = finalHtml;
  elSchedulesListFull.innerHTML = finalHtml;
}

function updatePatientDropdown() {
  let html = '<option value="">Select a patient...</option>';
  for (const [id, p] of Object.entries(patients)) {
    html += `<option value="${id}">${id} - ${p.name}</option>`;
  }
  elFormPatientId.innerHTML = html;
}

// ================= Battery Chart =================
function renderBatteryChart() {
  const labels = [];
  const data = [];
  const colors = [];

  for (const [id, dev] of Object.entries(devices)) {
    labels.push(dev.alias || id);
    const battery = dev.battery ?? 0;
    data.push(battery);
    colors.push(batteryColor(battery));
  }

  const config = {
    type: 'bar',
    data: {
      labels,
      datasets: [{
        label: 'Battery (%)',
        data,
        backgroundColor: colors,
        borderRadius: 8,
        maxBarThickness: 42,
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: { display: false },
        tooltip: {
          callbacks: { label: (ctx) => `${ctx.parsed.y}% baterai` }
        }
      },
      scales: {
        y: {
          min: 0, max: 100,
          ticks: { callback: (v) => v + '%', font: { family: 'IBM Plex Mono' } },
          grid: { color: 'rgba(15,23,42,0.06)' }
        },
        x: {
          ticks: { font: { family: 'Poppins' } },
          grid: { display: false }
        }
      }
    }
  };

  const ctx1 = document.getElementById('battery-chart');
  const ctx2 = document.getElementById('battery-chart-full');

  if (batteryChart) { batteryChart.data = config.data; batteryChart.update(); }
  else if (ctx1) batteryChart = new Chart(ctx1, config);

  if (batteryChartFull) { batteryChartFull.data = config.data; batteryChartFull.update(); }
  else if (ctx2) batteryChartFull = new Chart(ctx2, JSON.parse(JSON.stringify(config)));
}

// ================= Modal & Forms =================
function openModal(id) {
  document.getElementById(id).classList.add('active');
}

function closeModal(id) {
  document.getElementById(id).classList.remove('active');

  if (id === 'schedule-modal') {
    document.getElementById('schedule-form').reset();
    document.getElementById('edit-schedule-id').value = '';
    document.getElementById('form-patient-id').disabled = false;
  } else if (id === 'patient-modal') {
    document.getElementById('patient-form').reset();
    document.getElementById('edit-patient-id').value = '';
    document.getElementById('form-patient-new-id').disabled = false;
  } else if (id === 'assign-modal') {
    document.getElementById('assign-form').reset();
  }
}

function editSchedule(patientId, schedId) {
  const s = schedules[patientId][schedId];
  if (!s) return;

  document.getElementById('edit-schedule-id').value = schedId;
  document.getElementById('form-patient-id').value = patientId;
  document.getElementById('form-patient-id').disabled = true;
  document.getElementById('form-time').value = s.time;
  document.getElementById('form-qty1').value = s.qty_servo1;
  document.getElementById('form-qty2').value = s.qty_servo2;
  document.getElementById('form-status').value = s.status || 'Pending';

  openModal('schedule-modal');
}

async function deleteSchedule(patientId, schedId) {
  if (!confirm('Are you sure you want to delete this schedule?')) return;
  await fbDelete(`/schedules/${patientId}/${schedId}`);
  fetchSchedules();
}

document.getElementById('schedule-form').addEventListener('submit', async (e) => {
  e.preventDefault();

  const patientId = document.getElementById('form-patient-id').value;
  let schedId = document.getElementById('edit-schedule-id').value;

  if (!schedId) {
    schedId = 'sch_' + Math.floor(Math.random() * 1000000);
  }

  const data = {
    time: document.getElementById('form-time').value,
    qty_servo1: parseInt(document.getElementById('form-qty1').value),
    qty_servo2: parseInt(document.getElementById('form-qty2').value),
    status: document.getElementById('form-status').value
  };

  await fbPut(`/schedules/${patientId}/${schedId}`, data);
  closeModal('schedule-modal');
  fetchSchedules();
});

// ================= Patient Registration (CRUD) =================
const elPatientForm = document.getElementById('patient-form');
const elPatientModalTitle = document.getElementById('patient-modal-title');
const elFormPatientNewId = document.getElementById('form-patient-new-id');

function nextSuggestedPatientId() {
  const nums = Object.keys(patients)
    .map(id => parseInt((id.match(/\d+/) || ['0'])[0], 10))
    .filter(n => !isNaN(n));
  const next = (nums.length ? Math.max(...nums) : 0) + 1;
  return 'P-' + String(next).padStart(3, '0');
}

function openPatientModal() {
  document.getElementById('edit-patient-id').value = '';
  elPatientModalTitle.textContent = 'Daftarkan Pasien Baru';
  elPatientForm.reset();
  elFormPatientNewId.value = nextSuggestedPatientId();
  elFormPatientNewId.disabled = false;
  openModal('patient-modal');
}

function editPatient(id) {
  const p = patients[id];
  if (!p) return;

  document.getElementById('edit-patient-id').value = id;
  elPatientModalTitle.textContent = `Edit Pasien - ${id}`;
  elFormPatientNewId.value = id;
  elFormPatientNewId.disabled = true;
  document.getElementById('form-patient-name').value = p.name || '';
  document.getElementById('form-patient-age').value = p.age || '';
  document.getElementById('form-patient-gender').value = p.gender || 'male';
  document.getElementById('form-patient-disease').value = p.disease || '';
  document.getElementById('form-patient-wa').value = p.wa_number || '';
  document.getElementById('form-patient-stock1').value = p.stock_servo1 || 0;
  document.getElementById('form-patient-stock2').value = p.stock_servo2 || 0;
  document.getElementById('form-patient-threshold').value = p.stock_threshold || 5;

  openModal('patient-modal');
}

async function deletePatient(id) {
  if (!confirm(`Hapus data pasien ${id}? Jadwal terkait tidak akan otomatis terhapus.`)) return;
  await fbDelete(`/patients/${id}`);
  fetchPatients();
}

elPatientForm.addEventListener('submit', async (e) => {
  e.preventDefault();

  const existingId = document.getElementById('edit-patient-id').value;
  const newId = elFormPatientNewId.value.trim();
  const id = existingId || newId;

  if (!id) return;

  if (!existingId && patients[id]) {
    alert(`Patient ID "${id}" sudah digunakan. Gunakan ID lain.`);
    return;
  }

  const waRaw = document.getElementById('form-patient-wa').value.trim();
  const waDigits = waRaw.replace(/[^0-9]/g, '');

  const data = {
    name: document.getElementById('form-patient-name').value.trim(),
    age: parseInt(document.getElementById('form-patient-age').value),
    gender: document.getElementById('form-patient-gender').value,
    disease: document.getElementById('form-patient-disease').value.trim(),
    wa_number: waDigits ? Number(waDigits) : waRaw,
    stock_servo1: parseInt(document.getElementById('form-patient-stock1').value) || 0,
    stock_servo2: parseInt(document.getElementById('form-patient-stock2').value) || 0,
    stock_threshold: parseInt(document.getElementById('form-patient-threshold').value) || 5,
  };

  await fbPut(`/patients/${id}`, data);
  closeModal('patient-modal');
  fetchPatients();
});

// ================= Assign Patient to Device =================
const elAssignForm = document.getElementById('assign-form');
const elAssignDeviceLabel = document.getElementById('assign-device-label');
const elAssignPatientSelect = document.getElementById('assign-patient-select');

function openAssignModal(deviceId) {
  const dev = devices[deviceId];
  if (!dev) return;

  document.getElementById('assign-device-id').value = deviceId;
  elAssignDeviceLabel.textContent = `${dev.alias || deviceId} (${deviceId})`;

  let optionsHtml = '<option value="">-- Tidak ada (lepas pasangan) --</option>';
  for (const [id, p] of Object.entries(patients)) {
    optionsHtml += `<option value="${id}">${id} - ${p.name}</option>`;
  }
  elAssignPatientSelect.innerHTML = optionsHtml;
  elAssignPatientSelect.value = dev.assigned_patient || '';

  openModal('assign-modal');
}

elAssignForm.addEventListener('submit', async (e) => {
  e.preventDefault();

  const deviceId = document.getElementById('assign-device-id').value;
  const patientId = elAssignPatientSelect.value;

  if (!deviceId) return;

  await fbPut(`/devices/${deviceId}/assigned_patient`, patientId || null);
  closeModal('assign-modal');
  fetchDevices();
});

// ================= Init & Polling =================
async function init() {
  await fetchPatients();
  await fetchDevices();
  await fetchSchedules();

  pollTimer = setInterval(() => {
    fetchDevices();
    fetchSchedules();
  }, 5000);
}

// ================= Boot =================
if (isLoggedIn()) {
  showApp();
} else {
  showLogin();
}