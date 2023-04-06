import qrcode
import sqlite3
from flask import Flask

app = Flask(__name__)

@app.route("/")
def hello_world():
    return "<p>Hello, World!</p>"

if __name__ == "__main__":
    con = sqlite3.connect("users.db")
    cur = con.cursor()
    con.execute("CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT, seed_passwd TEXT, salt TEXT)")

    # username = input("Enter your username: ")
    # password = input("Enter your password: ")
    # cur.execute("INSERT INTO users (username, seed_passwd) VALUES (?, ?)", (username, password))
    # con.commit()
    # cur.execute("SELECT * FROM users")
    # print(cur.fetchall())

# img = qrcode.make('Some data here')
# type(img)  # qrcode.image.pil.PilImage
# img.save("some_file.png")