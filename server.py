from flask import Flask, request, jsonify, render_template_string
import sqlite3
import datetime
import json

app = Flask(__name__)

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Rover Vitals Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: 'Segoe UI', Tahoma, sans-serif; background-color: #f4f7f6; padding: 20px; }
        h1 { text-align: center; color: #2c3e50; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(400px, 1fr)); gap: 20px; max-width: 1200px; margin: auto; }
        .card { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
        .card h2 { margin-top: 0; color: #34495e; border-bottom: 2px solid #3498db; padding-bottom: 10px; }
        .vitals { display: flex; justify-content: space-between; margin-bottom: 15px; font-weight: bold; font-size: 18px;}
        canvas { max-height: 250px; }
    </style>
</head>
<body>
    <h1>🏥 Autonomous Rover Vitals Dashboard</h1>
    <div class="grid">
        {% for row in rows %}
        <div class="card">
            <h2>Patient Bed: {{ row[1] }} <span style="float:right; font-size:14px; color:gray">{{ row[6] }}</span></h2>
            <div class="vitals">
                <span style="color:#e74c3c">BPM: {{ row[2] }}</span>
                <span style="color:#3498db">SpO2: {{ row[3] }}%</span>
                <span style="color:#f39c12">Temp: {{ row[4] }}°C</span>
            </div>
            <canvas id="ecgChart{{ row[0] }}"></canvas>
            <script>
                const ctx{{ row[0] }} = document.getElementById('ecgChart{{ row[0] }}').getContext('2d');
                const ecgData{{ row[0] }} = JSON.parse('{{ row[5] }}');
                new Chart(ctx{{ row[0] }}, {
                    type: 'line',
                    data: {
                        labels: Array.from({length: ecgData{{ row[0] }}.length}, (_, i) => i),
                        datasets: [{
                            label: 'ECG Trace',
                            data: ecgData{{ row[0] }},
                            borderColor: '#2ecc71',
                            borderWidth: 1.5,
                            pointRadius: 0,
                            tension: 0.3
                        }]
                    },
                    options: { responsive: true, maintainAspectRatio: false, scales: { x: { display: false } } }
                });
            </script>
        </div>
        {% endfor %}
    </div>
</body>
</html>
"""

def init_db():
    conn = sqlite3.connect('hospital_vitals.db')
    c = conn.cursor()
    # Added ecg_data column
    c.execute('''CREATE TABLE IF NOT EXISTS patient_records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    bed_id INTEGER, bpm INTEGER, spo2 INTEGER, temp_c REAL,
                    ecg_data TEXT, timestamp TEXT
                )''')
    conn.commit()
    conn.close()

@app.route('/api/vitals', methods=['POST'])
def receive_vitals():
    try:
        # Get JSON safely
        data = request.get_json(silent=True)
        if data is None:
            print("❌ ERROR: Received Invalid or Incomplete JSON from ESP32!")
            print("Raw Data:", request.get_data())
            return jsonify({"status": "error", "message": "Invalid JSON"}), 400

        bed_id, bpm, spo2, temp = data.get('bed_id'), data.get('bpm'), data.get('spo2'), data.get('temp')
        ecg_array = data.get('ecg', []) 
        
        ecg_json_str = json.dumps(ecg_array) 
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        conn = sqlite3.connect('hospital_vitals.db')
        c = conn.cursor()
        c.execute("INSERT INTO patient_records (bed_id, bpm, spo2, temp_c, ecg_data, timestamp) VALUES (?, ?, ?, ?, ?, ?)", 
                  (bed_id, bpm, spo2, temp, ecg_json_str, timestamp))
        conn.commit()
        conn.close()
        print(f"✅ [{timestamp}] Saved -> Bed: {bed_id} | ECG Points: {len(ecg_array)}")
        return jsonify({"status": "success"}), 201

    except Exception as e:
        print(f"❌ DATABASE/SERVER ERROR: {str(e)}")
        return jsonify({"status": "error", "message": str(e)}), 400

@app.route('/')
def dashboard():
    conn = sqlite3.connect('hospital_vitals.db')
    c = conn.cursor()
    c.execute("SELECT * FROM patient_records ORDER BY id DESC")
    rows = c.fetchall()
    conn.close()
    return render_template_string(HTML_TEMPLATE, rows=rows)

if __name__ == '__main__':
    init_db()
    app.run(host='0.0.0.0', port=5000)
