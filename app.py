import uuid
import os
from flask import Flask, request, jsonify, session, render_template, redirect, flash, render_template_string
import psycopg2
from werkzeug.security import generate_password_hash, check_password_hash

app = Flask(__name__)
app.secret_key = os.urandom(24)
# PostgreSQL Connection Configuration
# Replace these values with your actual database credentials
DB_PARAMS = {
    "dbname": "postgres",
    "user": "postgres",
    "password": "17mar2010",
    "host": "localhost",
    "port": "5432"
}

def get_db_connection():
    return psycopg2.connect(**DB_PARAMS)

# Single HTML template handling both Login and Register toggles
HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Flask + Postgres Auth</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f4f9; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); width: 300px; }
        h2 { text-align: center; margin-bottom: 20px; color: #333; }
        input { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
        button { width: 100%; padding: 10px; background: #007BFF; border: none; color: white; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #0056b3; }
        .toggle-link { text-align: center; margin-top: 15px; font-size: 14px; color: #555; }
        .toggle-link span { color: #007BFF; cursor: pointer; text-decoration: underline; }
        .hidden { display: none; }
        .message { color: red; text-align: center; margin-bottom: 10px; }
    </style>
</head>
<body>
<div class="container">
    {% if msg %}<div class="message">{{ msg }}</div>{% endif %}

    <div id="login-box">
        <h2>Login</h2>
        <form action="/home" method="POST">
            <input type="text" name="username" placeholder="Username" required>
            <input type="password" name="password" placeholder="Password" required>
            <button type="submit">Sign In</button>
        </form>
        <div class="toggle-link">Don't have an account? <span onclick="toggleForm()">Register</span></div>
    </div>

    <div id="register-box" class="hidden">
        <h2>Register</h2>
        <form action="/register" method="POST">
            <input type="text" name="username" placeholder="Choose Username" required>
            <input type="password" name="password" placeholder="Choose Password" required>
            <button type="submit" style="background: #28a745;">Sign Up</button>
        </form>
        <div class="toggle-link">Already have an account? <span onclick="toggleForm()">Login</span></div>
    </div>
</div>

<script>
    function toggleForm() {
        document.getElementById('login-box').classList.toggle('hidden');
        document.getElementById('register-box').classList.toggle('hidden');
    }
</script>
</body>
</html>
"""
# http://localhost:5000/
@app.route('/')
def index():
    return render_template_string(HTML_TEMPLATE)

#Register Web Using HTTP POST http://localhost:5000/register
@app.route('/register', methods=['POST'])
def register():
    username = request.form.get('username')
    password = request.form.get('password')

    if not username or not password:
        return render_template_string(HTML_TEMPLATE, msg="Missing username or password.")

    # Hash the password securely
    hashed_password = generate_password_hash(password)

    conn = get_db_connection()
    cur = conn.cursor()
    try:
        cur.execute("INSERT INTO users (username, password) VALUES (%s, %s)", (username, hashed_password))
        conn.commit()
        return render_template_string(HTML_TEMPLATE, msg="Registration successful! Please login.")
    except psycopg2.errors.UniqueViolation:
        conn.rollback()
        return render_template_string(HTML_TEMPLATE, msg="Username already exists!")
    finally:
        cur.close()
        conn.close()
# login using POST  http://localhost:5000/login
@app.route('/home', methods=['POST'])
def login():
    username = request.form.get('username')
    password = request.form.get('password')

    conn = get_db_connection()
    cur = conn.cursor()
    cur.execute("SELECT id,password FROM users WHERE username = %s", (username,))
    user = cur.fetchone()
    print(user)
    cur.close()
    conn.close()

    if user and check_password_hash(user[1], password):
        # 🌟 FIXED: Save the user's authentic database ID into the tracking session
        session['user_id'] = user[0]
        session['username'] = username
        
        # Give them a simple landing screen with a button to go claim or log out
        return f"""
        <h1>Welcome back, {username}! Login successful.</h1>
        <p>Your session is now active. You can scan QR codes to claim points!</p>
        <a href="/logout"><button style="padding: 10px; background: red; color: white; border: none; border-radius:4px;">Log Out</button></a>
        """
    else:
        return render_template_string(HTML_TEMPLATE, msg="Invalid username or password.")
    
@app.route('/api/add-points', methods=['POST'])
def add_points():
    """Endpoint for the ESP32 to drop off trash points and receive a QR link."""
    data = request.get_json()
    
    if not data or 'points' not in data:
        return jsonify({"status": "error", "message": "Invalid payload"}), 400
        
    bin_id = data.get('bin_id', 'unknown_bin')
    points = data.get('points')
    
    # Generate a unique 8-character token for the QR code
    token = str(uuid.uuid4())[:8]
    
    conn = get_db_connection()
    cur = conn.cursor()
    try:
        # Save the ticket into the database we just created
        cur.execute(
            "INSERT INTO point_tickets (ticket_code, points, claimed) VALUES (%s, %s, FALSE);",
            (token, points)
        )
        conn.commit()
        
        # Build the dynamic URL. Replace this with your current live Ngrok URL!
        claim_url = f"https://rummage-array-baggie.ngrok-free.dev/claim?token={token}"
        
        # Send it back to the ESP32
        return jsonify({
            "status": "success",
            "claim_url": claim_url
        }), 200
        
    except Exception as e:
        conn.rollback()
        return jsonify({"status": "error", "message": str(e)}), 500
    finally:
        cur.close()
        conn.close()


@app.route('/claim')
def claim_points():
    """The web route users hit when scanning the QR code with their phones."""
    
    # 🚨 BYPASSING LOGIN WALL FOR LOCAL TESTING:
    # We comment this out so it stops booting you back to the login page '/'
    if 'user_id' not in session:
        flash("Please log in first to claim your points!")
        return redirect('/')
    current_user_id = session['user_id']
        
    # FORCE the app to log points directly to Tlezer (User ID 1)
    #current_user_id = 1 
    
    token = request.args.get('token')
    
    if not token:
        return "Missing token parameter", 400
        
    conn = get_db_connection()
    cur = conn.cursor()
    
    try:
        # 2. Fetch ticket status from the database
        cur.execute("SELECT points, claimed FROM point_tickets WHERE ticket_code = %s;", (token,))
        ticket = cur.fetchone()
        
        if not ticket:
            return render_template('claim_result.html', success=False, message="Invalid QR Code!")
            
        points, claimed = ticket[0], ticket[1]
        
        # 3. Check if it's already been scanned
        if claimed:
            return render_template('claim_result.html', success=False, message="This QR Code has already been claimed!")
            
        # 4. Atomic Update: Give points to user AND kill the ticket
        cur.execute("UPDATE users SET points = points + %s WHERE id = %s;", (points, current_user_id))
        cur.execute("UPDATE point_tickets SET claimed = TRUE, claimed_by_user_id = %s WHERE ticket_code = %s;", 
                    (current_user_id, token))
        
        conn.commit()
        
        # Get the username for a nice success message
        cur.execute("SELECT username FROM users WHERE id = %s;", (current_user_id,))
        username = cur.fetchone()[0]
        
        return render_template('claim_result.html', success=True, username=username, points=points)
        
    except Exception as e:
        conn.rollback()
        return render_template('claim_result.html', success=False, message=f"Database error: {str(e)}")
    finally:
        cur.close()
        conn.close()

@app.route('/logout')
def logout():
    session.clear()
    return redirect('/')

if __name__ == '__main__':
    app.run(debug=True, port=5000)