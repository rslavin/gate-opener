from extensions import db
from flask_login import UserMixin
from enum import Enum
import datetime

class Role(Enum):
    ADMIN = 'admin'
    TRUSTED_USER = 'trusted_user'
    USER = 'user'

class User(db.Model, UserMixin):
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(20), unique=True, nullable=False)
    password = db.Column(db.String(60), nullable=False)
    role = db.Column(db.Enum(Role), default=Role.USER, nullable=False)

class Token(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    code = db.Column(db.String(5), unique=True, nullable=False)
    note = db.Column(db.String(120), nullable=True)
    created_by = db.Column(db.Integer, db.ForeignKey('user.id'), nullable=False)
    creator = db.relationship('User', backref='created_codes', lazy=True)
    expires_at = db.Column(db.DateTime, nullable=False)
    created_at = db.Column(db.DateTime, default=datetime.datetime.utcnow(), nullable=False)

    def __str__(self):
        creator_username = self.creator.username if self.creator else "Unknown"
        return_str = f"'{self.code}' created by {creator_username}"
        if self.note:
            return f"{return_str} with note: '{self.note}'"
        return return_str

class LogEntry(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    user = db.Column(db.String(20), unique=False, nullable=False)
    create_at = db.Column(db.DateTime, default=datetime.datetime.utcnow, nullable=False)

