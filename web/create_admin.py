from app import app, db, User, Role
from flask_bcrypt import Bcrypt

bcrypt = Bcrypt(app)

def create_user(username, password):
    with app.app_context():
        hashed_password = bcrypt.generate_password_hash(password).decode('utf-8')
        user = User(username=username, password=hashed_password, role=Role.ADMIN)
        db.session.add(user)
        db.session.commit()
        print(f"Admin user {username} created successfully.")

if __name__ == "__main__":
    username = input("Enter username: ")
    password = input("Enter password: ")
    create_user(username, password)
