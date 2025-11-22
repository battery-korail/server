"use client";
import React, { useState, useEffect, useRef, useCallback } from 'react';

// API base URL은 환경변수로 주입 (Next.js 퍼블릭 변수 사용)
const FLASK_API_URL = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:5000';

const MAX_DATA_POINTS = 25;

// Chart.js 인스턴스를 관리할 타입 정의
interface ChartInstance {
    update: (mode?: string) => void;
    data: { labels: string[], datasets: { data: number[] }[] };
    destroy: () => void;
}

// 저장된 값 테이블 타입
interface SavedValue {
  id: number;
  dp_pa: number;
  created_at: string;
}

// 데이터 업데이트 유틸리티
const addData = (chart: ChartInstance | null, label: string, value: number) => {
    if (!chart) return;
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(value);

    if (chart.data.labels.length > MAX_DATA_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }

    chart.update('none');
};

const initialChartConfig = (label: string, color: string, min: number, max: number) => ({
    type: 'line',
    data: { labels: [], datasets: [{ label, data: [], borderColor: color, backgroundColor: `${color}40`, fill: false, tension: 0.3, pointRadius: 1, borderWidth: 2 }] },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        plugins: { legend: { display: false } },
        scales: {
            x: { title: { display: true, text: 'Time', color: '#6B7280' }, grid: { display: false } },
            y: { title: { display: true, text: label, color: '#6B7280' }, suggestedMin: min, suggestedMax: max }
        }
    }
});

export default function App() {
    const [currentGravity, setCurrentGravity] = useState<number | null>(null);
    const [currentLevel, setCurrentLevel] = useState<number | null>(null);
    const [isSocketConnected, setIsSocketConnected] = useState(false);

    const gravityChartRef = useRef<ChartInstance | null>(null);
    const levelChartRef = useRef<ChartInstance | null>(null);
    const gravityCanvasRef = useRef<HTMLCanvasElement>(null);
    const levelCanvasRef = useRef<HTMLCanvasElement>(null);

    const [savedValues, setSavedValues] = useState<SavedValue[]>([]);
    const [page, setPage] = useState(1);
    const [totalPages, setTotalPages] = useState(1);
    const [isLoadingSaved, setIsLoadingSaved] = useState(false);
    const [isSaving, setIsSaving] = useState(false);

    const [selectedDate, setSelectedDate] = useState<string>(''); // YYYY-MM-DD
    const [sortOrder, setSortOrder] = useState<'desc' | 'asc'>('desc'); // 최신순 기본

    // --- 저장된 값 조회 함수 ---
    const fetchSaved = useCallback(async (targetPage: number) => {
        setIsLoadingSaved(true);
        try {
            const params = new URLSearchParams();
            params.append('page', targetPage.toString());
            params.append('per_page', '5');
            if (selectedDate) params.append('date', selectedDate);
            params.append('order', sortOrder);

            const res = await fetch(`${FLASK_API_URL}/dp/saved?${params.toString()}`);
            const contentType = res.headers.get('content-type') || '';
            if (!res.ok) {
                const text = await res.text();
                throw new Error(`HTTP ${res.status} ${res.statusText}: ${text.slice(0, 200)}`);
            }
            if (!contentType.includes('application/json')) {
                const text = await res.text();
                throw new Error(`Unexpected content-type: ${contentType}. Body: ${text.slice(0, 200)}`);
            }
            const json = await res.json() as any;
            const list = Array.isArray(json?.data) ? (json.data as SavedValue[]) : [];
            setSavedValues(list);
            setPage(typeof json?.page === 'number' ? json.page : targetPage);
            setTotalPages(typeof json?.total_pages === 'number' ? json.total_pages : 1);
        } catch (err) {
            console.error("저장된 값 조회 실패:", err);
            setSavedValues([]);
        } finally {
            setIsLoadingSaved(false);
        }
    }, [selectedDate, sortOrder]);

    // 날짜 및 정렬 state 변화 시 자동 fetch
    useEffect(() => { fetchSaved(1); }, [selectedDate, sortOrder, fetchSaved]);

    // --- 현재 측정값 저장 ---
    const handleSave = async () => {
        if (currentGravity === null) { alert("저장할 데이터가 수신되지 않았습니다."); return; }
        setIsSaving(true);
        try {
            const res = await fetch(`${FLASK_API_URL}/dp/save`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({}) });
            const result = await res.json();
            if (res.ok) {
                alert(`✅ 저장 완료!\n비중 값: ${result.dp_pa.toFixed(3)}\n`);
                fetchSaved(page);
            } else { alert(`❌ 저장 실패: ${result.error || '알 수 없는 오류'}`); }
        } catch (err) {
            console.error(err);
            alert('❌ Flask 서버 연결 오류가 발생했습니다.');
        } finally { setIsSaving(false); }
    };

    // --- Chart.js + Socket.IO 초기화 ---
    useEffect(() => {
        if (typeof window === 'undefined') return;

        let socket: any;
        let Chart: any;

        const init = async () => {
            const chartModule = await import("chart.js/auto");
            Chart = chartModule.default;

            if (gravityCanvasRef.current && levelCanvasRef.current) {
                if (gravityChartRef.current) gravityChartRef.current.destroy();
                if (levelChartRef.current) levelChartRef.current.destroy();

                gravityChartRef.current = new Chart(gravityCanvasRef.current.getContext('2d'), initialChartConfig('비중 (SG)', 'rgb(75, 192, 192)', 1.10, 1.30));
                levelChartRef.current = new Chart(levelCanvasRef.current.getContext('2d'), initialChartConfig('액위 (Level %)', 'rgb(255, 99, 132)', 0, 100));
            }

            if ((window as any).io) {
            socket = (window as any).io(FLASK_API_URL, { transports: ['websocket', 'polling'] });


                console.log(socket);

                socket.on("connect", () => {
    console.log("Socket.IO connected:", socket.id);
    setIsSocketConnected(true); 
});

socket.on("disconnect", () => {
    console.log("Socket.IO disconnected");
    setIsSocketConnected(false); 
});

               socket.on('batteryUpdate', (data: { gravity: number, level: number }) => {
    console.log("Received raw:", data);
    setCurrentGravity(data.gravity);
    setCurrentLevel(data.level);

    if (gravityChartRef.current) {
        console.log("Adding gravity to chart:", data.gravity);
        addData(gravityChartRef.current, new Date().toLocaleTimeString(), data.gravity);
    }
    if (levelChartRef.current) {
        console.log("Adding level to chart:", data.level);
        addData(levelChartRef.current, new Date().toLocaleTimeString(), data.level);
    }
});
            }
        };

        init();

        return () => {
            if (socket) socket.disconnect();
            if (gravityChartRef.current) gravityChartRef.current.destroy();
            if (levelChartRef.current) levelChartRef.current.destroy();
        };
    }, []);

    const statusColor = isSocketConnected ? 'bg-green-500' : 'bg-red-500';
    const statusText = isSocketConnected ? '연결됨' : '연결 끊김';

    return (
        <div className="p-4 md:p-8 bg-gray-50 min-h-screen font-sans">
            <header className="mb-8 flex justify-between items-center">
                <h1 className="text-3xl font-extrabold text-gray-700">🚊 Korail 철도 차량 배터리 실시간 모니터링</h1>
                <div className="flex items-center space-x-2">
                    <span className={`h-3 w-3 rounded-full ${statusColor}`}></span>
                    <span className="text-sm font-medium text-gray-700">{statusText}</span>
                </div>
            </header>

            <div className="grid grid-cols-1 md:grid-cols-3 gap-6 mb-8">
                <DataCard title="현재 비중 (Specific Gravity)" value={currentGravity !== null ? currentGravity.toFixed(3) : 'N/A'} unit="SG" color="text-teal-600" bgColor="bg-teal-50" />
                <DataCard title="현재 액위 (Electrolyte Level)" value={currentLevel !== null ? currentLevel.toFixed(1) : 'N/A'} unit="%" color="text-rose-600" bgColor="bg-rose-50" />
                <div className="p-6 bg-white rounded-xl shadow-lg flex flex-col justify-center items-center">
                    <button onClick={handleSave} disabled={isSaving || currentGravity === null} className="w-full px-6 py-3 bg-indigo-600 text-white font-bold rounded-lg shadow-md hover:bg-indigo-700 transition duration-200 disabled:bg-indigo-400 disabled:cursor-not-allowed">
                        {isSaving ? 'DB 저장 중...' : '현재 측정값 DB에 저장'}
                    </button>
                    <p className="text-xs text-gray-500 mt-2">현재 비중 값을 저장합니다.</p>
                </div>
            </div>

            <div className="grid grid-cols-1 lg:grid-cols-2 gap-6 mb-8">
                <ChartContainer title="비중 (Specific Gravity) 실시간 값"><canvas ref={gravityCanvasRef}></canvas></ChartContainer>
                <ChartContainer title="액위 (Electrolyte Level) 실시간 값"><canvas ref={levelCanvasRef}></canvas></ChartContainer>
            </div>

            <SavedDataTable
                savedValues={savedValues}
                isLoadingSaved={isLoadingSaved}
                page={page}
                totalPages={totalPages}
                setSelectedDate={setSelectedDate}
                selectedDate={selectedDate}
                sortOrder={sortOrder}
                setSortOrder={setSortOrder}
                fetchSaved={fetchSaved}
            />
        </div>
    );
}

// --- Components ---
const DataCard: React.FC<{ title: string; value: string; unit: string; color: string; bgColor: string; }> = ({ title, value, unit, color, bgColor }) => (
    <div className={`p-6 ${bgColor} rounded-xl shadow-lg `}>
        <p className="text-sm font-medium text-gray-600">{title}</p>
        <div className="mt-1 flex items-baseline">
            <span className={`text-4xl font-extrabold ${color}`}>{value}</span>
            <span className="ml-2 text-base font-semibold text-gray-500">{unit}</span>
        </div>
    </div>
);

const ChartContainer: React.FC<{ title: string; children: React.ReactNode }> = ({ title, children }) => (
    <div className="bg-white rounded-xl shadow-lg p-6">
        <h3 className="text-lg font-semibold mb-4 text-gray-700">{title}</h3>
        <div style={{ height: '300px' }}>{children}</div>
    </div>
);

interface SavedDataTableProps {
    savedValues: SavedValue[];
    isLoadingSaved: boolean;
    page: number;
    totalPages: number;
    selectedDate: string;
    setSelectedDate: React.Dispatch<React.SetStateAction<string>>;
    sortOrder: 'asc' | 'desc';
    setSortOrder: React.Dispatch<React.SetStateAction<'asc' | 'desc'>>;
    fetchSaved: (page: number) => void;
}

const SavedDataTable: React.FC<SavedDataTableProps> = ({ savedValues, isLoadingSaved, page, totalPages, selectedDate, setSelectedDate, sortOrder, setSortOrder, fetchSaved }) => (
    <div className="bg-white rounded-xl shadow-lg p-6">
        <h2 className="text-xl font-bold mb-4 text-gray-700">저장된 데이터 기록</h2>

        {/* 필터 UI */}
        <div className="flex justify-between mb-4 items-center">
            <div className="flex items-center space-x-2">
                <label className="text-gray-700 text-sm">날짜:</label>
                <input
                    type="date"
                    value={selectedDate}
                    onChange={(e) => setSelectedDate(e.target.value)}
                    className="border rounded px-2 py-1 text-sm"
                />
            </div>
            <button
                onClick={() => setSortOrder(prev => prev === 'desc' ? 'asc' : 'desc')}
                className="px-3 py-1 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 transition text-sm"
            >
                {sortOrder === 'desc' ? '최신순' : '오래된순'}
            </button>
        </div>

        {isLoadingSaved ? (
            <div className="text-center py-10 text-gray-500">데이터를 불러오는 중입니다...</div>
        ) : (
            <>
                <div className="overflow-x-auto">
                    <table className="w-full text-left">
                        <thead>
                            <tr className="bg-gray-100/80">
                                <th className="px-4 py-3 text-sm font-semibold text-gray-600 rounded-tl-lg">ID</th>
                                <th className="px-4 py-3 text-sm font-semibold text-gray-600">비중 값 (SG)</th>
                                <th className="px-4 py-3 text-sm font-semibold text-gray-600 rounded-tr-lg">저장 시간</th>
                            </tr>
                        </thead>
                        <tbody>
                            {Array.isArray(savedValues) && savedValues.length > 0 ? savedValues.map(v => (
                                <tr key={v.id} className="border-b border-gray-200 last:border-b-0 hover:bg-indigo-50/50 transition">
                                    <td className="px-4 py-3 text-sm text-gray-800">{v.id}</td>
                                    <td className="px-4 py-3 text-base font-semibold text-gray-900">{v.dp_pa.toFixed(3)}</td>
                                    <td className="px-4 py-3 text-sm text-gray-600">{new Date(v.created_at).toLocaleString()}</td>
                                </tr>
                            )) : (
                                <tr>
                                    <td colSpan={3} className="text-center py-6 text-gray-500">저장된 데이터가 없습니다.</td>
                                </tr>
                            )}
                        </tbody>
                    </table>
                </div>

                {/* 페이지네이션 */}
                {totalPages > 1 && (
                    <div className="mt-6 flex justify-between items-center">
                        <button
                            onClick={() => fetchSaved(page - 1)}
                            disabled={page <= 1 || isLoadingSaved}
                            className="px-4 py-2 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 transition disabled:opacity-50"
                        >&larr; 이전</button>
                        <span className="font-semibold text-gray-700">페이지 {page} / {totalPages}</span>
                        <button
                            onClick={() => fetchSaved(page + 1)}
                            disabled={page >= totalPages || isLoadingSaved}
                            className="px-4 py-2 bg-gray-100 text-gray-600 rounded-lg hover:bg-gray-200 transition disabled:opacity-50"
                        >다음 &rarr;</button>
                    </div>
                )}
            </>
        )}
    </div>
);