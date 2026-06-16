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
    "dbname": "",
    "user": "",
    "password": "",
    "host": "",
    "port": ""
}

def get_db_connection():
    return psycopg2.connect(**DB_PARAMS)

# http://localhost:5000/
@app.route('/')
def index():
    return render_template('login_3.html')

#Register Web Using HTTP POST http://localhost:5000/register
@app.route('/register', methods=['POST'])
def register():
    username = request.form.get('username')
    password = request.form.get('password')

    if not username or not password:
        return render_template('login_3.html', msg="Missing username or password.")

    # Hash the password securely
    hashed_password = generate_password_hash(password)

    conn = get_db_connection()
    cur = conn.cursor()
    try:
        cur.execute("INSERT INTO users (username, password) VALUES (%s, %s)", (username, hashed_password))
        conn.commit()
        return render_template('login_3.html', msg="Registration successful! Please login.")
    except psycopg2.errors.UniqueViolation:
        conn.rollback()
        return render_template('login_3.html', msg="Username already exists!")
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
        if 'user_id' in session:
         conn = get_db_connection()
         cur = conn.cursor()
        
        # 2. Run a SELECT query to grab the live points counter
        cur.execute("SELECT username, points FROM users WHERE id = %s;", (session['user_id'],))
        row = cur.fetchone()
        
        cur.close()
        conn.close()
        
        # 3. Pack the tuple values into a clean Python dictionary
        if row:
            user_data = {
                "username": row[0],  # index 0 from SELECT
                "points": row[1]    # index 1 from SELECT
            }
        return render_template('home.html', user_data=user_data)
    else:
        return render_template('login_3.html', msg="Invalid username or password.")
    
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