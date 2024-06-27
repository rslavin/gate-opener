from flask import Flask, request, jsonify, render_template, redirect, url_for
from flask_sqlalchemy import SQLAlchemy
from flask_bcrypt import Bcrypt
from flask_login import LoginManager, UserMixin, login_user, login_required, logout_user, current_user
from itsdangerous import URLSafeTimedSerializer
import datetime
import random
import string
from open_gate import open_gate
from dotenv import load_dotenv
import os
import logging
from logging.handlers import RotatingFileHandler
from enum import Enum
from decorators import role_required, roles_required


load_dotenv()

app = Flask(__name__)
app.config['SECRET_KEY'] = os.getenv('SECRET_KEY')
app.config['SQLALCHEMY_DATABASE_URI'] = os.getenv('SQLALCHEMY_DATABASE_URI')
db = SQLAlchemy(app)
bcrypt = Bcrypt(app)
login_manager = LoginManager(app)
login_manager.login_view = 'login'
serializer = URLSafeTimedSerializer(app.config['SECRET_KEY'])


# User model

class Role(Enum):
    ADMIN = 'admin'
    TRUSTED_USER = 'trusted_user'
    USER = 'user'


class User(db.Model, UserMixin):
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(20), unique=True, nullable=False)
    password = db.Column(db.String(60), nullable=False)
    role = db.Column(db.Enum(Role), default=Role.USER, nullable=False)


# Token model
class Token(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    code = db.Column(db.String(5), unique=True, nullable=False)
    note = db.Column(db.String(120), nullable=True)
    created_by = db.Column(db.Integer, db.ForeignKey('user.id'), nullable=False)
    creator = db.relationship('User', backref='created_codes', lazy=True)
    expires_at = db.Column(db.DateTime, nullable=False)
    created_at = db.Column(db.DateTime, default=datetime.datetime.utcnow(), nullable=False)

@app.route('/delete_code/<int:code_id>', methods=['POST'])
@roles_required(Role.TRUSTED_USER, Role.ADMIN)
@login_required
def delete_code(code_id):
    code = Token.query.get(code_id)
    if code:
        db.session.delete(code)
        db.session.commit()
    return redirect(url_for('grant_access'))

@login_manager.user_loader
def load_user(user_id):
    return User.query.get(int(user_id))

@app.context_processor
def inject_roles():
    return dict(Role=Role)

# Set up logging
if not app.debug:
    file_handler = RotatingFileHandler('error.log', maxBytes=10240, backupCount=10)
    file_handler.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        '%(asctime)s %(levelname)s: %(message)s [in %(pathname)s:%(lineno)d]')
    file_handler.setFormatter(formatter)
    app.logger.addHandler(file_handler)


@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        # Handle form data
        if request.form:
            username = request.form['username']
            password = request.form['password']
        # Handle JSON data
        else:
            data = request.get_json()
            if not data:
                return jsonify({'message': 'Invalid input'}), 400
            username = data['username']
            password = data['password']

        user = User.query.filter_by(username=username).first()
        if user and bcrypt.check_password_hash(user.password, password):
            login_user(user)
            return redirect(url_for('home'))
        else:
            return jsonify({'message': 'Login unsuccessful. Please check username and password'}), 401
    return render_template('login.html')


@app.route('/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('login'))


@app.route('/')
@login_required
def home():
    return render_template('index.html')


@app.route('/open', methods=['POST'])
@login_required
def open():
    response = open_gate()
    return jsonify({'message': 'Gate opened', 'response': response}), 200


@app.route('/grant_access', methods=['GET', 'POST'])
@roles_required(Role.TRUSTED_USER, Role.ADMIN)
@login_required
def grant_access():
    if request.method == 'POST':
        duration = request.form.get('duration')
        note = request.form.get('note')
        expires_at = datetime.datetime.utcnow() + datetime.timedelta(hours=int(duration))
        if request.form.get('code'):
            code = request.form.get('code')
        else:
            code = ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
        access_code = Token(code=code, note=note, expires_at=expires_at, created_by=current_user.id)
        db.session.add(access_code)
        db.session.commit()
        return redirect(url_for('grant_access'))

    codes = Token.query.all()
    return render_template('grant_access.html', codes=codes)


@app.route('/code', methods=['GET', 'POST'], strict_slashes=False)
def code_page():
    if request.method == 'POST':
        code = request.form['code']
        token = Token.query.filter_by(code=code).first()
        if token and token.expires_at > datetime.datetime.utcnow():
            response = open_gate()
            remaining_time = token.expires_at - datetime.datetime.utcnow()
            if remaining_time < datetime.timedelta(days=30):
                return jsonify({'message': 'Gate opened', 'response': response, 'time_left': str(remaining_time)}), 200
            return jsonify({'message': 'Gate opened', 'response': response}), 200
        return jsonify({'message': 'Invalid or expired code'}), 401
    return render_template('code.html', code='')


@app.route('/code/<string:code>', methods=['GET', 'POST'], strict_slashes=False)
def code_with_segment(code):
    if request.method == 'POST':
        token = Token.query.filter_by(code=code).first()
        if token and token.expires_at > datetime.datetime.utcnow():
            response = open_gate()
            remaining_time = token.expires_at - datetime.datetime.utcnow()
            if remaining_time < datetime.timedelta(days=30):
                return jsonify({'message': 'Gate opened', 'response': response, 'time_left': str(remaining_time)}), 200
            return jsonify({'message': 'Gate opened', 'response': response}), 200
        return jsonify({'message': 'Invalid or expired code'}), 401
    return render_template('code.html', code=code)


@app.route('/admin', methods=['GET', 'POST'])
@role_required(Role.ADMIN)
@login_required
def admin():

    if request.method == 'POST':
        # Add user
        username = request.form['username']
        password = request.form['password']
        hashed_password = bcrypt.generate_password_hash(password).decode('utf-8')
        user = User(username=username, password=hashed_password)
        db.session.add(user)
        db.session.commit()
        return redirect(url_for('admin'))

    users = User.query.all()
    roles = Role
    return render_template('admin.html', users=users, roles=roles)


@app.route('/delete_user/<int:user_id>', methods=['POST'])
@role_required(Role.ADMIN)
@login_required
def delete_user(user_id):
    user = User.query.get(user_id)
    if user:
        db.session.delete(user)
        db.session.commit()
    return redirect(url_for('admin'))


@app.route('/create_user', methods=['POST'])
def create_user_route():
    data = request.get_json()
    hashed_password = bcrypt.generate_password_hash(data['password']).decode('utf-8')
    user = User(username=data['username'], password=hashed_password)
    db.session.add(user)
    db.session.commit()
    return jsonify({'message': 'User created successfully'}), 201


@app.route('/change_role/<int:user_id>', methods=['POST'])
@role_required(Role.ADMIN)
@login_required
def change_role(user_id):
    user = User.query.get(user_id)
    if user:
        user.role = Role(request.form['role'])
        db.session.commit()
    return redirect(url_for('admin'))


if __name__ == '__main__':
    app.run(debug=True)
