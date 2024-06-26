from flask import Flask
from extensions import db, bcrypt, login_manager, migrate
from config import Config
from routes import main_blueprint, auth_blueprint, admin_blueprint
import logging
import pytz
from logging.handlers import RotatingFileHandler

app = Flask(__name__)
app.config.from_object(Config)

db.init_app(app)
bcrypt.init_app(app)
login_manager.init_app(app)
migrate.init_app(app, db)
login_manager.login_view = 'auth.login'

app.register_blueprint(main_blueprint)
app.register_blueprint(auth_blueprint)
app.register_blueprint(admin_blueprint)

# Set the timezone
timezone = pytz.timezone('US/Central')

# Convert datetime to the specified timezone
@app.template_filter('datetime')
def format_datetime(value):
    if value.tzinfo is None:
        value = pytz.utc.localize(value)
    local_time = value.astimezone(timezone)
    return local_time.strftime('%B %d, %Y - %I:%M:%S%p')

@login_manager.user_loader
def load_user(user_id):
    from models import User
    return User.query.get(int(user_id))

@app.context_processor
def inject_roles():
    from models import Role
    return dict(Role=Role)

# Set up logging
if not app.debug:
    file_handler = RotatingFileHandler('error.log', maxBytes=10240, backupCount=10)
    file_handler.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s %(levelname)s: %(message)s [in %(pathname)s:%(lineno)d]')
    file_handler.setFormatter(formatter)
    app.logger.addHandler(file_handler)

if __name__ == '__main__':
    app.run(debug=True)