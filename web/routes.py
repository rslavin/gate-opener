from flask import Blueprint, request, jsonify, render_template, redirect, url_for
from flask_login import login_user, login_required, logout_user, current_user
from models import User, Token, Role
from extensions import db, bcrypt
from decorators import role_required, roles_required
import datetime
import random
import string
from open_gate import open_gate

main_blueprint = Blueprint('main', __name__)
auth_blueprint = Blueprint('auth', __name__)
admin_blueprint = Blueprint('admin', __name__)

@main_blueprint.route('/')
@login_required
def home():
    return render_template('index.html')

@main_blueprint.route('/open', methods=['POST'])
@login_required
def open():
    response = open_gate()
    return jsonify({'message': 'Gate opened', 'response': response}), 200

@main_blueprint.route('/grant_access', methods=['GET', 'POST'])
@roles_required(Role.TRUSTED_USER, Role.ADMIN)
@login_required
def grant_access():
    if request.method == 'POST':
        duration = request.form.get('duration')
        note = request.form.get('note')
        expires_at = datetime.datetime.utcnow() + datetime.timedelta(hours=int(duration))
        code = request.form.get('code') if request.form.get('code') else ''.join(random.choices(string.ascii_uppercase + string.digits, k=5))
        access_code = Token(code=code, note=note, expires_at=expires_at, created_by=current_user.id)
        db.session.add(access_code)
        db.session.commit()
        return redirect(url_for('main.grant_access'))

    codes = Token.query.all()
    return render_template('grant_access.html', codes=codes)

@main_blueprint.route('/delete_code/<int:code_id>', methods=['POST'])
@roles_required(Role.TRUSTED_USER, Role.ADMIN)
@login_required
def delete_code(code_id):
    code = Token.query.get(code_id)
    if code:
        db.session.delete(code)
        db.session.commit()
    return redirect(url_for('main.grant_access'))

@main_blueprint.route('/code', methods=['GET', 'POST'], strict_slashes=False)
@main_blueprint.route('/code/<string:code>', methods=['GET', 'POST'], strict_slashes=False)
def code_page(code=''):
    if request.method == 'POST':
        token = Token.query.filter_by(code=code).first()
        if token and token.expires_at > datetime.datetime.utcnow():
            response = open_gate()
            remaining_time = token.expires_at - datetime.datetime.utcnow()
            return jsonify({'message': 'Gate opened', 'response': response, 'time_left': str(remaining_time)}), 200
        return jsonify({'message': 'Invalid or expired code'}), 401
    return render_template('code.html', code=code)

@auth_blueprint.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        user = User.query.filter_by(username=username).first()
        if user and bcrypt.check_password_hash(user.password, password):
            login_user(user)
            return redirect(url_for('main.home'))
        else:
            return jsonify({'message': 'Login unsuccessful. Please check username and password'}), 401
    return render_template('login.html')

@auth_blueprint.route('/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('auth.login'))

@admin_blueprint.route('/admin', methods=['GET', 'POST'])
@role_required(Role.ADMIN)
@login_required
def admin():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        role = request.form['role']
        hashed_password = bcrypt.generate_password_hash(password).decode('utf-8')
        user = User(username=username, password=hashed_password, role=Role(role))
        db.session.add(user)
        db.session.commit()
        return redirect(url_for('admin.admin'))

    users = User.query.all()
    roles = Role
    return render_template('admin.html', users=users, roles=roles)

@admin_blueprint.route('/delete_user/<int:user_id>', methods=['POST'])
@role_required(Role.ADMIN)
@login_required
def delete_user(user_id):
    user = User.query.get(user_id)
    if user:
        db.session.delete(user)
        db.session.commit()
    return redirect(url_for('admin.admin'))

@admin_blueprint.route('/change_role/<int:user_id>', methods=['POST'])
@role_required(Role.ADMIN)
@login_required
def change_role(user_id):
    user = User.query.get(user_id)
    if user:
        user.role = Role(request.form['role'])
        db.session.commit()
    return redirect(url_for('admin.admin'))
