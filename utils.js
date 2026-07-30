// ================================================================
// UTILITY FUNCTIONS
// ================================================================

// --- Auth Guard ---
function requireAuth(allowedRoles) {
   const user = JSON.parse(sessionStorage.getItem('currentUser') || 'null');
   if (!user) {
      window.location.href = '/index.html';
      return null;
   }
   if (allowedRoles && !allowedRoles.includes(user.role)) {
      window.location.href = '/index.html';
      return null;
   }
   return user;
}

function getCurrentUser() {
   return JSON.parse(sessionStorage.getItem('currentUser') || 'null');
}

function logout() {
   auth.signOut().then(() => {
      sessionStorage.removeItem('currentUser');
      window.location.href = '/index.html';
   });
}

// --- Toast Notifications ---
function showToast(message, type = 'info') {
   let container = document.getElementById('toastContainer');
   if (!container) {
      container = document.createElement('div');
      container.id = 'toastContainer';
      container.className = 'toast-container';
      document.body.appendChild(container);
   }

   const icons = { success: '✅', error: '❌', info: 'ℹ️' };
   const toast = document.createElement('div');
   toast.className = `toast ${type}`;
   toast.innerHTML = `<span>${icons[type] || icons.info}</span><span>${message}</span>`;
   container.appendChild(toast);

   setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateX(20px)';
      toast.style.transition = 'all 0.3s';
      setTimeout(() => toast.remove(), 300);
   }, 3500);
}

// --- Render Sidebar based on role ---
function renderSidebar(activeItem) {
   const user = getCurrentUser();
   if (!user) return;

   const menus = {
      admin: [
         { id: 'dashboard', icon: '📊', label: 'Dashboard',       href: 'dashboard.html' },
         { id: 'sesi',      icon: '▶️',  label: 'Sesi Absensi',   href: 'sesi.html' },
         { id: 'absensi',   icon: '📋', label: 'Rekap Absensi',   href: 'absensi.html' },
         { id: 'mapel',     icon: '📚', label: 'Mata Pelajaran',  href: 'mapel.html' },
         { id: 'siswa',     icon: '🎓', label: 'Data Siswa',      href: 'siswa.html' },
         { id: 'guru',      icon: '👤', label: 'Manajemen Guru',  href: 'guru.html' },
      ],
      guru: [
         { id: 'dashboard', icon: '📊', label: 'Dashboard',       href: 'dashboard-guru.html' },
         { id: 'sesi',      icon: '▶️',  label: 'Sesi Absensi',   href: 'sesi.html' },
         { id: 'absensi',   icon: '📋', label: 'Rekap Absensi',   href: 'absensi.html' },
         { id: 'siswa',     icon: '🎓', label: 'Registrasi Siswa', href: 'siswa.html' },
      ],
   };
   const roleMenus = menus[user.role] || menus.guru;
   const sidebarNav = document.getElementById('sidebarNav');
   if (!sidebarNav) return;

   sidebarNav.innerHTML = `
    <div class="nav-section-label">MENU UTAMA</div>
    ${roleMenus
       .map(
          (m) => `
      <button class="nav-item ${m.id === activeItem ? 'active' : ''}" onclick="window.location.href='${m.href}'">
        <span class="nav-icon">${m.icon}</span>
        <span>${m.label}</span>
      </button>
    `,
       )
       .join('')}
  `;

   // Update user info
   const userEl = document.getElementById('sidebarUser');
   if (userEl) {
      userEl.innerHTML = `
      <div class="user-info">
        <div class="user-avatar">${(user.name || user.email)[0].toUpperCase()}</div>
        <div>
          <div class="user-name">${user.name || 'User'}</div>
          <div class="user-role">${user.role === 'admin' ? '👑 Admin' : '📚 Guru'}</div>
        </div>
      </div>
      <button class="btn btn-secondary btn-sm w-full" onclick="logout()">🚪 Logout</button>
    `;
   }
}

// --- Mobile Sidebar Toggle ---
function initMobileSidebar() {
   const hamburger = document.getElementById('hamburger');
   const sidebar = document.getElementById('sidebar');
   const mobileOverlay = document.getElementById('mobileOverlay');

   if (!hamburger || !sidebar) return;

   hamburger.addEventListener('click', () => {
      sidebar.classList.toggle('open');
      mobileOverlay.classList.toggle('show');
   });

   if (mobileOverlay) {
      mobileOverlay.addEventListener('click', () => {
         sidebar.classList.remove('open');
         mobileOverlay.classList.remove('show');
      });
   }
}

// --- Date Helpers ---
function formatDate(dateStr) {
   if (!dateStr) return '-';
   const d = new Date(dateStr);
   return d.toLocaleDateString('id-ID', { day: '2-digit', month: 'long', year: 'numeric' });
}

function formatTime(ts) {
   if (!ts) return '-';
   // ESP32 simpan epoch DETIK. Jika < 10^10 → detik, kalikan 1000. Jika >= 10^10 → ms (data lama).
   const tsMs = ts < 1e10 ? ts * 1000 : ts;
   // NTPClient.getEpochTime() dengan offset WIB sudah menyertakan +7jam di dalam nilai epoch.
   // Jadi JANGAN tambah offset lagi — langsung baca pakai getUTCHours() agar tidak double-offset.
   const d = new Date(tsMs);
   const hh = String(d.getUTCHours()).padStart(2, '0');
   const mm = String(d.getUTCMinutes()).padStart(2, '0');
   const ss = String(d.getUTCSeconds()).padStart(2, '0');
   return hh + '.' + mm + '.' + ss;
}

function todayString() {
   return new Date().toISOString().split('T')[0]; // YYYY-MM-DD
}

function getMonthRange(year, month) {
   // month: 1-12
   const start = new Date(year, month - 1, 1);
   const end = new Date(year, month, 0);
   return {
      start: start.toISOString().split('T')[0],
      end: end.toISOString().split('T')[0],
   };
}

// --- Export Excel (SheetJS via CDN) ---
function exportToExcel(data, headers, filename) {
   const ws_data = [headers, ...data];
   const ws = XLSX.utils.aoa_to_sheet(ws_data);

   // Auto column width
   const wscols = headers.map((h) => ({ wch: Math.max(h.length + 4, 15) }));
   ws['!cols'] = wscols;

   const wb = XLSX.utils.book_new();
   XLSX.utils.book_append_sheet(wb, ws, 'Data');
   XLSX.writeFile(wb, `${filename}.xlsx`);
   showToast('File Excel berhasil didownload!', 'success');
}

// --- Export PDF (jsPDF) ---
function exportToPDF(title, headers, rows, filename) {
   const { jsPDF } = window.jspdf;
   const doc = new jsPDF();

   doc.setFontSize(14);
   doc.setFont('helvetica', 'bold');
   doc.text(title, 14, 20);

   doc.setFontSize(9);
   doc.setFont('helvetica', 'normal');
   doc.text(`Dicetak: ${new Date().toLocaleString('id-ID')}`, 14, 28);

   doc.autoTable({
      head: [headers],
      body: rows,
      startY: 35,
      styles: { fontSize: 9, cellPadding: 3 },
      headStyles: { fillColor: [37, 99, 235], textColor: 255, fontStyle: 'bold' },
      alternateRowStyles: { fillColor: [245, 248, 255] },
      margin: { top: 35 },
   });

   doc.save(`${filename}.pdf`);
   showToast('File PDF berhasil didownload!', 'success');
}


// --- Absensi Sesi Helpers ---
// Cek apakah waktu tap (epochSec dari ESP32) terlambat dari jamMulai + toleransi
// jamMulai: string "HH:MM", toleransi: menit, tanggal: "YYYY-MM-DD"
function hitungStatus(epochSec, jamMulai, toleransi, tanggal) {
   if (!jamMulai) return 'hadir'; // tanpa jam mulai = selalu hadir
   const [jam, menit] = jamMulai.split(':').map(Number);
   const batasMs = new Date(tanggal).getTime() + (jam * 60 + menit + (toleransi || 0)) * 60 * 1000;
   // epochSec dari NTPClient sudah include offset WIB, jadi bandingkan langsung
   const tapMs   = epochSec * 1000;
   // Normalisasi: kurangi WIB offset agar jadi UTC, lalu bandingkan dengan batas WIB
   // Batas: tanggal (UTC midnight) + offset WIB + jam+menit+toleransi dalam ms
   const wibOffsetMs = 7 * 60 * 60 * 1000;
   const batasWIBms  = new Date(tanggal + 'T00:00:00Z').getTime() + wibOffsetMs + (jam * 60 + menit + (toleransi || 0)) * 60 * 1000;
   return tapMs > batasWIBms ? 'terlambat' : 'hadir';
}

// --- Firebase Auth State ---
auth.onAuthStateChanged((firebaseUser) => {
   const publicPages = ['index.html', '/'];
   const currentPath = window.location.pathname;
   const isPublic = publicPages.some((p) => currentPath.endsWith(p) || currentPath === p);

   if (!firebaseUser && !isPublic) {
      window.location.href = '/index.html';
   }
});
